#!/usr/bin/env python3

from __future__ import absolute_import, division, print_function, unicode_literals
from . import invoke_rpc_builtin, invoke_rpc_python_udf

from . import ProcessGroupAgent
from .internal_rpc_utils import serialize, PythonUDF

import array
import sys
import torch
from enum import Enum


_agent = None


def _collect_worker_names(name, group):
    from . import all_gather
    from . import get_world_size

    # collect name length
    ws = get_world_size(group)
    name_bytes = list(array.array('B', bytes(name, 'utf8')))
    name_len = len(name_bytes)
    len_input = torch.ones(1, dtype=torch.int64) * name_len
    len_outputs = [torch.empty(1, dtype=torch.int64) for _ in range(ws)]
    all_gather(len_outputs, len_input, group=group)

    # collect name value
    max_len = torch.stack(len_outputs).max().item()
    name_input = torch.empty(max_len, dtype=torch.uint8)
    name_input[:name_len] = torch.tensor(name_bytes, dtype=torch.uint8)
    name_outputs = [torch.empty(max_len, dtype=torch.uint8) for _ in range(ws)]
    all_gather(name_outputs, name_input, group=group)

    names = []
    for i in range(ws):
        name_tensor = name_outputs[i][:len_outputs[i]]
        names.append(bytearray(name_tensor.tolist()).decode('utf8'))

    return names


def join_rpc():
    r"""
    Block until all local and remote RPC processes reach this method, process
    (send and receive) all pending messages, and then destroy local RPC agent.
    Every RPC process must call this method before exit.
    """
    global _agent

    if _agent:
        _agent.join()
        _agent = None


def sync_rpc():
    r"""
    Block until all local and remote RPC processes reach this method and finish
    sending all pending RPCs. As this method synchronizes at the process
    level, if multiple threads are spawned, only one of them should call this
    method at a time.
    """
    if _agent is None:
        raise RuntimeError("RPC has not been initialized. "
                           "Call init_model_parallel() first.")

    _agent.sync()

class RpcBackend(Enum):
    PROCESS_GROUP = 1

# TODO: add a context managet to wrap _init_rpc and join_rpc
def _init_rpc(name, backend=RpcBackend.PROCESS_GROUP):
    if sys.version_info < (3, 0):
        raise RuntimeError("RPC package does not support Python2.")

    global _agent

    if _agent:
        raise RuntimeError("RPC is already initialized")

    if backend == RpcBackend.PROCESS_GROUP:
        from .distributed_c10d import _get_default_group
        group = _get_default_group()
        # TODO: issue #23232
        names = _collect_worker_names(name, group)
        name_dict = {names[r] : r for r in range(len(names))}
        _agent = ProcessGroupAgent(name, name_dict, group)
    else:
        raise RuntimeError("Unrecognized RPC backend ", backend)


def rpc(to, func, args=None, kwargs=None, async_call=False):
    r"""
    Make an RPC call to run function ``func`` on worker ``to``. By default, it
    blocks until the return value is locally available. RPC messages are sent
    and received in parallel to execution of Python code. This method is
    thread-safe.

    Arguments:
        to (str): name of the destination worker.
        func (callable): any callable function. builtin functions (like torch.add) can be sent over RPC more efficiently.
        args (tuple): the argument tuple for the ``func`` invocation.
        kwargs (dict): is a dictionary of keyword arguments for the ``func``
                       invocation.
        async_call (bool): If set to ``True``, this will be an asynchronous RPC,
                           and returns a ``torch.distributed.FutureMessage``
                           object immediately. Otherwise, this RPC will block
                           until the return value is locally available.
                           (default: ``False``)

    Returns:
        If ``async_call`` is ``False``, returns the result of running ``func``
        on ``args`` and ``kwargs``. If ``async_call`` is ``True``, returns a
        ``torch.distributed.FutureMessage`` object that can be waited on. When
        completed, the return value of ``func`` on ``args`` and ``kwargs`` can
        be retrieved from the ``FutureMessage`` object.

    Example::

        Synchronous example:

        On worker 0:
        >>> import torch.distributed as dist
        >>> dist.init_process_group(backend='gloo', rank=0, world_size=2)
        >>> dist.init_model_parallel("worker0")
        >>> ret = dist.rpc("worker1", torch.add, args=(torch.ones(2), 3))
        >>> dist.join_rpc()

        One worker 1:
        >>> import torch.distributed as dist
        >>> dist.init_process_group(backend='gloo', rank=1, world_size=2)
        >>> dist.init_model_parallel("worker1")
        >>> dist.join_rpc()

        Asynchronous example:

        On worker 0:
        >>> import torch.distributed as dist
        >>> dist.init_process_group(backend='gloo', rank=0, world_size=2)
        >>> dist.init_model_parallel("worker0")
        >>> fut1 = dist.rpc("worker1", torch.add, args=(torch.ones(2), 3), async_call=True)
        >>> fut2 = dist.rpc("worker1", min, args=(1, 2), async_call=True)
        >>> result = fut1.wait() + fut2.wait()
        >>> dist.join_rpc()

        One worker 1:
        >>> import torch.distributed as dist
        >>> dist.init_process_group(backend='gloo', rank=1, world_size=2)
        >>> dist.init_model_parallel("worker1")
        >>> dist.join_rpc()
    """
    if not callable(func):
        raise TypeError("function should be callable.")

    if _agent is None:
        raise RuntimeError("RPC has not been initialized. "
                           "Call init_model_parallel() first.")

    qualified_name = torch.jit._find_builtin(func)

    args = args if args else ()
    kwargs = kwargs if kwargs else {}
    if qualified_name is not None:
        fut = invoke_rpc_builtin(_agent, to, qualified_name, *args, **kwargs)
    else:
        fut = invoke_rpc_python_udf(_agent, to, serialize(PythonUDF(func, args, kwargs)))

    if async_call:
        return fut
    else:
        return fut.wait()

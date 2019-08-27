#pragma once

#include <torch/csrc/distributed/rpc/future_message.h>
#include <torch/csrc/distributed/rpc/message.h>

namespace torch {
namespace distributed {
namespace rpc {

using worker_id_t = int16_t;

// A globally unique ID to identify an RpcAgent
struct WorkerId {
  WorkerId(std::string name, int id)
      : name_(std::move(name)), id_(id) {
    TORCH_CHECK(id <= std::numeric_limits<worker_id_t>::max(),
        "RPC worker id ", id, " out of bound of int16_t.");
  }

  WorkerId(std::string name, worker_id_t id)
      : name_(std::move(name)), id_(id) {}

  const std::string name_;
  const worker_id_t id_;
};

// ``RpcAgent`` is the base class for sending and receiving RPC messages. It
// provides a unified ``send`` API for both request and response messages, and
// will invoke the given ``RequestCallback`` to process received requests. It
// should immediately become ready to serve request and accept response after
// construction.
class RpcAgent;

// ``RpcAgent`` implementation should invoke ``RequestCallback`` to process
// received requests. There is no restriction on the implementation's threading
// model. This function takes the ``WorkerId`` of the request sender, the an
// rvalue reference of the Message object, and a reference to the ``RpcAgent``.
// Having a reference to the ``RpcAgent`` allows the ``RequestCallback``
// implementation to be both stateless and non-blocking. For example, it may
// enqueue the message and the ``RpcAgent`` reference, and use a different pool
// of threads to process them later.
using RequestCallback =
    std::function<void(const WorkerId&, Message&&, RpcAgent&)>;

class RpcAgent {
 public:
  // `WorkerId` is the globally unique identifier for this RpcAgent instance. It
  // contains a ``name_`` field and an ``id_`` field. ``name_`` is the globally
  // unique name for this ``RpcAgent``. It is up to the ``RpcAgent``
  // implementation to determine how to resolve names. ``id_`` is the globally
  // unique ID for this ``RpcAgent``. This should be determined by the
  // ``RpcAgent`` implementation.
  // The ``RequestCallback`` will be invoked to handle received requests. This
  // ``RpcAgent`` base class makes no assumption on the thread-safeness of the
  // ``RequestCallback``. ``RpcAgent`` implementations need to make sure that
  // its threading model conform to ``RequestCallback``'s requirement.
  RpcAgent(WorkerId id, RequestCallback cb);

  virtual ~RpcAgent();

  // Send a message to the ``RpcAgent`` of id ``to`` and returns a
  // ``FutureMessage`` ptr. The implementation must be asynchronous, i.e., it
  // cannot block until it receives the response.
  //
  // If ``message.isRequest()`` is true, the ``FutureMessage`` will be completed
  // when the response arrives. For other message types, the Future should be
  // ignored by the caller.
  virtual std::shared_ptr<FutureMessage> send(
      const WorkerId& to, Message&& message) = 0;

  // Return a reference to the ``WorkerId`` of this RpcAgent.
  // NB: not using ``c10::optional<const std::string&>`` here because we might
  // need to create a separate RPC API lib and avoid forcing all ``RpcAgent``
  // implementations to depend on libtorch.
  const WorkerId& getWorkerId() const;

  // Return a reference to the ``WorkerId`` of the given ``workerName``.
  virtual const WorkerId& getWorkerId(const std::string& workerName) const = 0;

  // Call sync and join all internal threads. This method should be called
  // before every RPC process exits.
  virtual void join() = 0;

  // Synchronize the this process with other ``RpcAgent`` processes. Block until
  // all ``RpcAgent``s reach this method and send all pending messages.
  virtual void sync() = 0;

 protected:
  const WorkerId workerId_;
  const RequestCallback cb_;
};

}
}
}

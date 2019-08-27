#include <torch/csrc/distributed/autograd/context/dist_autograd_container.h>
#include <torch/csrc/jit/pybind_utils.h>
#include <torch/csrc/python_headers.h>
#include <torch/csrc/utils/object_ptr.h>
#include <torch/csrc/utils/pybind.h>
#include <torch/types.h>

namespace torch {
namespace distributed {
namespace autograd {

namespace {

template <typename T>
using shared_ptr_class_ = py::class_<T, std::shared_ptr<T>>;

PyObject* dist_autograd_init(PyObject* /* unused */) {
  auto dist_module =
      THPObjectPtr(PyImport_ImportModule("torch.distributed.autograd"));
  if (!dist_module) {
    throw python_error();
  }

  auto module = py::handle(dist_module).cast<py::module>();

  auto distAutogradContext =
      shared_ptr_class_<DistAutogradContext>(module, "DistAutogradContext")
          .def(
              "_context_id",
              &DistAutogradContext::context_id,
              py::call_guard<py::gil_scoped_release>());

  module.def("_new_context", []() {
    return DistAutogradContainer::getInstance().newContext();
  });

  module.def("_release_context", [](int64_t context_id) {
    return DistAutogradContainer::getInstance().releaseContext(context_id);
  });

  module.def("_retrieve_context", [](int64_t context_id) {
    return DistAutogradContainer::getInstance().retrieveContext(context_id);
  });

  Py_RETURN_TRUE;
}
} // namespace

static PyMethodDef methods[] = { // NOLINT
    {"_dist_autograd_init",
     (PyCFunction)dist_autograd_init,
     METH_NOARGS,
     nullptr},
    {nullptr, nullptr, 0, nullptr}};

PyMethodDef* python_functions() {
  return methods;
}

} // namespace autograd
} // namespace distributed
} // namespace torch

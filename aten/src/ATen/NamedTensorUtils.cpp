#ifdef BUILD_NAMEDTENSOR

#include <ATen/NamedTensorUtils.h>
#include <bitset>
#include <sstream>

namespace at {

// Returns "Tensor['N', 'C', 'H', 'W']" for a tensor with names ('N', 'C', 'H', 'W').
static std::string toDimnameRepr(const Tensor& tensor) {
  std::ostringstream os;
  os << "Tensor" << tensor.names();
  return os.str();
}

int64_t dimname_to_position(const Tensor& tensor, Dimname dim) {
  TORCH_CHECK(dim.type() != NameType::WILDCARD,
      "Please look up dimensions by name, got: name = None.");
  TORCH_CHECK(tensor.has_names(),
      "Name ", dim, " not found in ", toDimnameRepr(tensor), ".");
  const auto names = tensor.names();

  const auto it = std::find_if(
      names.begin(), names.end(),
      [&dim](const Dimname& candidate) { return dim.can_refer_to(candidate); });
  TORCH_CHECK(it != names.end(),
      "Name ", dim, " not found in ", toDimnameRepr(tensor), ".");

  // Check that it can't refer to another dimension
  const auto dup = std::find_if(
      it + 1, names.end(),
      [&dim](const Dimname& candidate) { return dim.can_refer_to(candidate); });
  TORCH_CHECK(
      dup == names.end(),
      "Name ", dim, " could refer to multiple dimensions in ",
      toDimnameRepr(tensor), ". Please disambiguate by using a more ",
      "specific name like ", *it, " or ", dup, ".");
  return std::distance(names.begin(), it);
}

std::vector<int64_t> dimnames_to_positions(const Tensor& tensor, DimnameList dims) {
  std::vector<int64_t> result;
  result.reserve(dims.size());
  for (const auto& name : dims) {
    result.push_back(dimname_to_position(tensor, name));
  }
  return result;
}

static void report_positional_error(
    const Dimname& name,
    const Dimname& other_name,
    DimnameList names,
    DimnameList other_names) {
  // TODO(zou3519): Can improve message by checking if names are alignable and suggesting workarounds
  TORCH_CHECK(false,
      "Names ", name, " and ", other_name, " do not match positionally ",
      "from the right in names ", names, " and ", other_names, ".");
}

static void check_for_misalignment(
    const Dimname& name,
    DimnameList names,
    DimnameList other_names) {
  if (name.is_wildcard()) {
    return;
  }
  auto it = std::find_if(other_names.begin(), other_names.end(),
      [&](const Dimname& candidate) { return name.can_refer_to(candidate); });
  // TODO(zou3519): Can improve message by checking if names are alignable and suggesting workarounds
  TORCH_CHECK(it == other_names.end(),
      "Names ", names, " and ", other_names, " are misaligned: name ", name,
      " appears in a different position from the right.");
}

// Assumption: A DimnameList can have no duplicate full names with
// the exception of wildcards
std::vector<Dimname> unify_from_right(DimnameList names, DimnameList other_names) {
  const auto wildcard = Dimname::wildcard();
  const auto size = std::max(names.size(), other_names.size());
  auto result = std::vector<Dimname>(size, wildcard);

  auto names_it = names.rbegin();
  auto other_it = other_names.rbegin();
  auto result_it = result.rbegin();
  while (names_it != names.rend() || other_it != other_names.rend()) {
    const auto& name = names_it == names.rend() ? wildcard : *names_it;
    const auto& other_name = other_it == other_names.rend() ? wildcard : *other_it;

    // TODO(zou3519): Don't support tagged names for now. They're a little weird.
    if (name.is_tagged() || other_name.is_tagged()) {
      TORCH_INTERNAL_ASSERT("unify_from_right: NYI: tagged names.");
    }

    // Step 1: Check that the names match
    const auto maybeName = unify(name, other_name);
    if (!maybeName) {
      report_positional_error(name, other_name, names, other_names);
    }
    *result_it = *maybeName;

    // Step 2: Check that the names are not misaligned
    if (!name.is_normal() || !other_name.is_normal()) {
      // Let: N = max(len(names), len(other_names))
      //      K = # of special names among names and other_names.
      // This search (including the outer loop) is O(N*K) but typically # of dims is small.
      check_for_misalignment(name, names, other_names);
      check_for_misalignment(other_name, other_names, names);
    }

    if (names_it != names.rend()) {
      ++names_it;
    }
    if (other_it != other_names.rend()) {
      ++other_it;
    }
    ++result_it;
  }
  return result;
}


namespace namedinference {

static std::bitset<kMaxNamedTensorDim>
compute_included_idxs(IntArrayRef excluded_idxs) {
  std::bitset<kMaxNamedTensorDim> included_idxs;
  for (auto i : excluded_idxs) {
    TORCH_INTERNAL_ASSERT(
        i <= kMaxNamedTensorDim,
        "Only tensors with up to ", kMaxNamedTensorDim, " are supported.");
    included_idxs.set(i);
  }
  included_idxs.flip();
  return included_idxs;
}

static void assert_names_equal(DimnameList a, DimnameList b) {
  TORCH_CHECK(a == b,
      "Name mismatch: specified out tensor with names ", a,
      " are not the same as the computed output names ", b,
      ". Please rename the out tensor's dimensions.");
}

void propagate_names(TensorImpl* result, optional<DimnameList> names) {
  if (!impl::has_names(result) && !names.has_value()) {
    return;
  }
  if (!impl::has_names(result)) {
    impl::internal_set_names_inplace(result, names);
    return;
  }
  assert_names_equal(
      impl::get_names(result),
      names.value_or(default_names(result->dim())));
}

void propagate_names(TensorImpl* result, std::vector<Dimname>&& names, bool validate_names) {
  if (!impl::has_names(result)) {
    impl::internal_set_names_inplace(result, std::move(names), validate_names);
    return;
  }
  assert_names_equal(impl::get_names(result), names);
}

void propagate_names(Tensor& result, optional<DimnameList> names) {
  propagate_names(result.unsafeGetTensorImpl(), names);
}

void propagate_names(Tensor& result, std::vector<Dimname>&& names, bool validate_names) {
  propagate_names(result.unsafeGetTensorImpl(), std::move(names), validate_names);
}

void propagate_names_except(Tensor& result, const Tensor& src, IntArrayRef excluded_idxs) {
  if (!result.has_names() && !src.has_names()) {
    return;
  }
  auto src_names = src.names();
  auto result_dim = result.dim();
  auto src_dim = src_names.size();
  TORCH_INTERNAL_ASSERT(src_dim - excluded_idxs.size() == result_dim);

  // fast path
  if (excluded_idxs.size() == 1) {
    std::vector<Dimname> outnames = src_names.vec();
    outnames.erase(outnames.begin() + excluded_idxs[0]);
    propagate_names(result, std::move(outnames), /*validate_names=*/false);
    return;
  }

  std::vector<Dimname> outnames;
  outnames.reserve(result_dim);
  auto included_idxs = compute_included_idxs(excluded_idxs);
  for (size_t dim = 0; dim < src_dim; ++dim) {
    if (included_idxs[dim]) {
      outnames.push_back(src_names[dim]);
    }
  }
  propagate_names(result, std::move(outnames), /*validate_names=*/false);
}

void propagate_names_for_reduction(Tensor& result, const Tensor& src, IntArrayRef reduced_dims, bool keepdim) {
  if (keepdim) {
    propagate_names(result, src);
    return;
  }
  // This actually means "full reduction"
  if (reduced_dims.size() == 0) {
    return;
  }
  propagate_names_except(result, src, reduced_dims);
}

void propagate_names(Tensor& result, const Tensor& src) {
  propagate_names(result.unsafeGetTensorImpl(), src.unsafeGetTensorImpl());
}

void propagate_names(TensorImpl* result, TensorImpl* src) {
  if (result == src) {
    return;
  }
  propagate_names(result, impl::get_opt_names(src));
}

void propagate_names_for_copy(Tensor& result, const Tensor& src) {
  if (!result.has_names() && !src.has_names()) {
    return;
  }
  auto outnames = unify_from_right(result.names(), src.names());
  propagate_names(result, std::move(outnames), /*validate_names=*/false);
}

// tensor_dotted_dim and other_dotted_dim are the dimensions of the two
// tensors that we contract together. Usually other_dotted_dim is 0
// and tensor_dotted_dim is the last dim of tensor, but there are some special
// cases like einsum and tensordot where one can contract arbitrary dims.
static std::vector<Dimname> compute_dot_product_outnames(
    DimnameList tensor_names,
    int64_t tensor_dotted_dim,
    DimnameList other_names,
    int64_t other_dotted_dim) {
  int64_t num_outnames = tensor_names.size() + other_names.size() - 2;
  if (num_outnames == 0) {
    return {};
  }
  std::vector<Dimname> outnames(num_outnames, Dimname::wildcard());
  int64_t index = 0;
  for (int64_t j = 0; j < tensor_names.size(); ++j) {
    if (j == tensor_dotted_dim) continue;
    outnames[index++] = tensor_names[j];
  }
  for (int64_t j = 0; j < other_names.size(); ++j) {
    if (j == other_dotted_dim) continue;
    outnames[index++] = other_names[j];
  }
  return outnames;
}

static optional<DimnameList> to_opt_dimnames(
    const optional<std::vector<Dimname>>& names) {
  if (names) {
    return *names;
  }
  return nullopt;
}

void propagate_names_for_addmv(
    TensorImpl* result,
    TensorImpl* mat,
    TensorImpl* vec,
    TensorImpl* bias) {
  if (!impl::has_names(result) && !impl::has_names(mat) &&
      !impl::has_names(vec) && !impl::has_names(bias)) {
    return;
  }
  auto mv_outnames = compute_dot_product_outnames(
      impl::get_names(mat),
      /*tensor_dotted_dim=*/1,
      impl::get_names(vec),
      /*other_dotted_dim=*/0);
  auto add_outnames = unify_from_right(mv_outnames, impl::get_names(bias));
  propagate_names(result, std::move(add_outnames), /*validate_names=*/false);
}

void propagate_names_for_addmm(
    TensorImpl* result,
    TensorImpl* m1,
    TensorImpl* m2,
    TensorImpl* bias) {
  if (!impl::has_names(m1) && !impl::has_names(m2) &&
      !impl::has_names(bias) && !impl::has_names(result)) {
    return;
  }
  auto mm_outnames = compute_dot_product_outnames(
      impl::get_names(m1),
      /*tensor_dotted_dim=*/1,
      impl::get_names(m2),
      /*other_dotted_dim=*/0);
  TORCH_CHECK(
    mm_outnames[0] == Dimname::wildcard() || mm_outnames[0] != mm_outnames[1],
    "Matrix multiplying Tensor", impl::get_names(m1),
    " with Tensor", impl::get_names(m2),
    " would produce output tensor with duplicate names ",
    "[", mm_outnames[0], ", ",  mm_outnames[1], "]",
    ". Please rename the input tensors to prevent this.");
  auto add_outnames = unify_from_right(mm_outnames, impl::get_names(bias));
  propagate_names(result, std::move(add_outnames), /*validate_names=*/false);
}

void check_names_for_dot(
    TensorImpl* vec1,
    TensorImpl* vec2) {
  if (!impl::has_names(vec1) && !impl::has_names(vec2)) {
    return;
  }
  compute_dot_product_outnames(
      impl::get_names(vec1),
      /*tensor_dotted_dim=*/0,
      impl::get_names(vec2),
      /*other_dotted_dim=*/0);
}

// expand adds new None dimensions. This is consistent with name inference
// rules for binary ops that expect the named dims to line up positionally
// from the right. i.e.,
// Tensor[H, W].expand(3, 3, 3, 3) -> Tensor[None, None, H, W]
void propagate_names_for_expand(Tensor& result, const Tensor& self) {
  if (!self.has_names()) {
    return;
  }
  auto result_dim = result.dim();
  if (self.dim() == result_dim) {
    propagate_names(result, self);
    return;
  }
  std::vector<Dimname> outnames(result_dim, Dimname::wildcard());
  std::copy(
      self.opt_names()->begin(),
      self.opt_names()->end(),
      outnames.begin() + result_dim - self.dim());
  propagate_names(result, std::move(outnames), /*validate_names=*/false);
}

} // namespace namedinference
} // namespace at
#endif

#pragma once

#include <c10/util/Optional.h>
#include <torch/csrc/distributed/rpc/message.h>
#include <torch/csrc/distributed/rpc/rpc_agent.h>
#include <torch/csrc/distributed/rpc/rref.h>
#include <torch/csrc/distributed/rpc/types.h>

#include <atomic>

namespace torch {
namespace distributed {
namespace rpc {

// Manages RRef lifetime and keeps track of RRef forks.
class RRefContext {
 public:
  static std::unique_ptr<RRefContext>& getInstance(
      std::shared_ptr<RpcAgent> = nullptr);

  RRefContext(const RRefContext &) = delete;
  void operator=(const RRefContext &) = delete;

  worker_id_t getWorkerId() const;
  RRefId genRRefId();
  const std::shared_ptr<RpcAgent> agent() const;

  template <typename T>
  std::shared_ptr<RRef> createRRef(worker_id_t ownerId) {
    if (ownerId == getWorkerId()) {
      return getOrCreateOwnerRRef<T>(genRRefId());
    } else {
      return createUserRRef<T>(ownerId, genRRefId(), genRRefId());
    }
  }

  template <typename T>
  std::shared_ptr<RRef> getOrCreateRRef(const at::IValue&& value) {
    auto rfd = RRefForkData::fromIValue(std::move(value));
    return getOrCreateRRef<T>(rfd.ownerId_, rfd.rrefId_, rfd.forkId_);
  }

  template <typename T>
  std::shared_ptr<RRef> getOrCreateRRef(
      worker_id_t ownerId, RRefId rrefId, ForkId forkId) {
    if (ownerId == getWorkerId()) {
      return getOrCreateOwnerRRef<T>(rrefId);
    } else {
      return createUserRRef<T>(ownerId, rrefId, forkId);
    }
  }

  template <typename T>
  std::shared_ptr<RRef> createUserRRef(
      worker_id_t ownerId, RRefId rrefId, ForkId forkId) {
    TORCH_CHECK(ownerId != getWorkerId(), "RRef owner cannot create user RRef.");
    // RRefContext does not track user RRefs, it will be destructed when there is
    // no shared_ptrs pointing to it.
    return std::make_shared<RRefImpl<T>>(RRefImpl<T>(ownerId, rrefId, forkId));
  }

  template <typename T>
  std::shared_ptr<RRef> getOrCreateOwnerRRef(RRefId rrefId) {

    std::lock_guard<std::mutex> lock(mutex_);
    const auto iter = rrefs_.find(rrefId);
    if (iter == rrefs_.end()) {
      // Scenario (1) the first time this owner knows about this RRef
      // Scenario (2) This owner is also the creator.
      // Creating an RRef
      auto rref = std::make_shared<RRefImpl<T>>(
          RRefImpl<T>(getWorkerId(), rrefId, rrefId));
      rrefs_[rref->id()] = rref;
      return rref;

    } else {
      // Scenario (3) retrieving an existing RRef
      return iter->second;;
    }
  }

  void addFork(const at::IValue&& value);
  void delFork(const at::IValue&& value);

 private:
  RRefContext(std::shared_ptr<RpcAgent>);

  static std::unique_ptr<RRefContext> context_;
  const std::shared_ptr<RpcAgent> agent_;
  static std::atomic<local_id_t> nextLocalId_;
  std::mutex mutex_;
  std::unordered_map<RRefId, std::shared_ptr<RRef>, RRefId::Hash> rrefs_;
  std::unordered_map<RRefId,
                     std::unordered_set<RRefId, RRefId::Hash>,
                     RRefId::Hash> forks_;
};

} // namespace rpc
} // namespace distributed
} // namespace torch

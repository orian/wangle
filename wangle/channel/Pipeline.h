/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <wangle/channel/HandlerContext.h>
#include <folly/futures/Future.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/DelayedDestruction.h>
#include <folly/ExceptionWrapper.h>
#include <folly/Memory.h>

namespace folly { namespace wangle {

class PipelineManager {
 public:
  virtual ~PipelineManager() {}
  virtual void deletePipeline(PipelineBase* pipeline) = 0;
};

class PipelineBase {
 public:
  virtual ~PipelineBase() {}

  void setPipelineManager(PipelineManager* manager) {
    manager_ = manager;
  }

  void deletePipeline() {
    if (manager_) {
      manager_->deletePipeline(this);
    }
  }

  void setTransport(std::shared_ptr<AsyncTransport> transport) {
    transport_ = transport;
  }

  std::shared_ptr<AsyncTransport> getTransport() {
    return transport_;
  }

 private:
  PipelineManager* manager_{nullptr};
  std::shared_ptr<AsyncTransport> transport_;
};

struct Nothing{};

/*
 * R is the inbound type, i.e. inbound calls start with pipeline.read(R)
 * W is the outbound type, i.e. outbound calls start with pipeline.write(W)
 *
 * Use Nothing for one of the types if your pipeline is unidirectional.
 * If R is Nothing, read(), readEOF(), and readException() will be disabled.
 * If W is Nothing, write() and close() will be disabled.
 */
template <class R, class W = Nothing>
class Pipeline : public PipelineBase, public DelayedDestruction {
 public:
  typedef std::unique_ptr<Pipeline, Destructor> UniquePtr;

  Pipeline();
  ~Pipeline();

  void setWriteFlags(WriteFlags flags);
  WriteFlags getWriteFlags();

  void setReadBufferSettings(uint64_t minAvailable, uint64_t allocationSize);
  std::pair<uint64_t, uint64_t> getReadBufferSettings();

  template <class T = R>
  typename std::enable_if<!std::is_same<T, Nothing>::value>::type
  read(R msg);

  template <class T = R>
  typename std::enable_if<!std::is_same<T, Nothing>::value>::type
  readEOF();

  template <class T = R>
  typename std::enable_if<!std::is_same<T, Nothing>::value>::type
  readException(exception_wrapper e);

  template <class T = R>
  typename std::enable_if<!std::is_same<T, Nothing>::value>::type
  transportActive();

  template <class T = R>
  typename std::enable_if<!std::is_same<T, Nothing>::value>::type
  transportInactive();

  template <class T = W>
  typename std::enable_if<!std::is_same<T, Nothing>::value, Future<Unit>>::type
  write(W msg);

  template <class T = W>
  typename std::enable_if<!std::is_same<T, Nothing>::value, Future<Unit>>::type
  close();

  template <class H>
  Pipeline& addBack(std::shared_ptr<H> handler);

  template <class H>
  Pipeline& addBack(H&& handler);

  template <class H>
  Pipeline& addBack(H* handler);

  template <class H>
  Pipeline& addFront(std::shared_ptr<H> handler);

  template <class H>
  Pipeline& addFront(H&& handler);

  template <class H>
  Pipeline& addFront(H* handler);

  template <class H>
  H* getHandler(int i);

  void finalize();

  // If one of the handlers owns the pipeline itself, use setOwner to ensure
  // that the pipeline doesn't try to detach the handler during destruction,
  // lest destruction ordering issues occur.
  // See thrift/lib/cpp2/async/Cpp2Channel.cpp for an example
  template <class H>
  bool setOwner(H* handler);

 protected:
  explicit Pipeline(bool isStatic);

  template <class Context>
  void addContextFront(Context* ctx);

  void detachHandlers();

 private:
  template <class Context>
  Pipeline& addHelper(std::shared_ptr<Context>&& ctx, bool front);

  WriteFlags writeFlags_{WriteFlags::NONE};
  std::pair<uint64_t, uint64_t> readBufferSettings_{2048, 2048};

  bool isStatic_{false};
  std::shared_ptr<PipelineContext> owner_;
  std::vector<std::shared_ptr<PipelineContext>> ctxs_;
  std::vector<PipelineContext*> inCtxs_;
  std::vector<PipelineContext*> outCtxs_;
  InboundLink<R>* front_{nullptr};
  OutboundLink<W>* back_{nullptr};
};

}}

namespace folly {

class AsyncSocket;

template <typename Pipeline>
class PipelineFactory {
 public:
  virtual typename Pipeline::UniquePtr newPipeline(
      std::shared_ptr<AsyncSocket>) = 0;

  virtual ~PipelineFactory() {}
};

}

#include <wangle/channel/Pipeline-inl.h>

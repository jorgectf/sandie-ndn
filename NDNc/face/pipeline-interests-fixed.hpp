/*
 * N-DISE: NDN for Data Intensive Science Experiments
 * Author: Catalin Iordache <catalin.iordache@cern.ch>
 *
 * MIT License
 *
 * Copyright (c) 2021 California Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef NDNC_FACE_PIPELINE_INTERESTS_FIXED_HPP
#define NDNC_FACE_PIPELINE_INTERESTS_FIXED_HPP

#include <unordered_map>

#include "pipeline-interests.hpp"

namespace ndnc {
class PipelineFixed : public Pipeline {
  public:
    using PendingInteretsTable = std::unordered_map<uint64_t, PendingInterest>;

  public:
    PipelineFixed(Face &face, size_t size);
    ~PipelineFixed();

  private:
    void run() final;

    void processInterest(PendingInterest &&);
    void processInterests(std::vector<PendingInterest> &&, size_t);
    void processTimeout();

    void replyWithData(std::shared_ptr<ndn::Data> &&, uint64_t);
    void replyWithError(PendingInterestResultError, uint64_t);

  public:
    bool enqueueInterestPacket(std::shared_ptr<ndn::Interest> &&interest,
                               void *rxQueue) final;

    bool
    enqueueInterests(std::vector<std::shared_ptr<ndn::Interest>> &&interests,
                     size_t n, void *rxQueue) final;

    void dequeueDataPacket(std::shared_ptr<ndn::Data> &&data,
                           ndn::lp::PitToken &&pitToken) final;

    void dequeueNackPacket(std::shared_ptr<ndn::lp::Nack> &&nack,
                           ndn::lp::PitToken &&pitToken) final;

  private:
    size_t m_maxSize;

    TxQueue m_tasksQueue;
    PendingInteretsTable m_pit;
};
}; // namespace ndnc

#endif // NDNC_FACE_PIPELINE_INTERESTS_FIXED_HPP
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

#ifndef NDNC_FACE_PACKET_HANDLER_HPP
#define NDNC_FACE_PACKET_HANDLER_HPP

#include <map>
#include <unordered_map>

#include <ndn-cxx/data.hpp>
#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/lp/nack.hpp>
#include <ndn-cxx/lp/pit-token.hpp>

#include "lp/pit-token.hpp"

namespace ndnc {
class Face;
}
#include "face.hpp"

namespace ndnc {
class PacketHandler {
  public:
    explicit PacketHandler(Face &face);

  protected:
    virtual ~PacketHandler();
    bool removePendingInterestEntry(uint64_t pitToken);

  private:
    void doLoop();

    void onData(const std::shared_ptr<const ndn::Data> &data,
                const ndn::lp::PitToken &pitToken);

  public:
    /**
     * @brief Override to be invoked periodically
     *
     */
    virtual void loop();

    /**
     * @brief Override to send Interest packets
     *
     * @param interest the Interest packet to send
     * @return true packet has been sent
     * @return false packet could not be sent
     */
    virtual uint64_t
    expressInterest(const std::shared_ptr<const ndn::Interest> &interest);

    /**
     * @brief Override to send Data packets
     *
     * @param data the Data packet to send
     * @param pitToken the PIT token of the Interest that this Data packet
     * satisfies
     * @return true
     * @return false
     */
    virtual bool putData(const ndn::Data &&data,
                         const ndn::lp::PitToken &pitToken);

    /**
     * @brief Override to receive Interest packets
     *
     * @param interest the Interest packet
     * @param pitToken the PIT token of this Interest
     */
    virtual void
    processInterest(const std::shared_ptr<const ndn::Interest> &interest,
                    const ndn::lp::PitToken &pitToken);

    /**
     * @brief Override to receive Data packets
     *
     * @param data the Data packet
     * @param pitToken the PIT token of this Data
     */
    virtual void processData(const std::shared_ptr<const ndn::Data> &data,
                             uint64_t pitToken);

    /**
     * @brief Override to receive NACK packets
     *
     * @param nack the NACK packet
     */
    virtual void processNack(const std::shared_ptr<const ndn::lp::Nack> &nack);

    /**
     * @brief Override to receive timeout notifications
     *
     * @param pitToken  the PIT token of Interest that timeouts
     */
    virtual void onTimeout(uint64_t pitToken);

  private:
    Face *m_face;
    std::shared_ptr<ndnc::lp::PitTokenGenerator> m_pitTokenGen;

    std::unordered_map<uint64_t, ndn::time::system_clock::time_point>
        m_pitToInterestLifetime;
    std::map<ndn::time::system_clock::time_point, uint64_t>
        m_interestLifetimeToPit;

    friend Face;
};
}; // namespace ndnc

#endif // NDNC_FACE_PACKET_HANDLER_HPP

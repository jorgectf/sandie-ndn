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

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <unistd.h>

#include <boost/algorithm/string.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

#include "ft-client-utils.hpp"
#include "ft-client.hpp"
#include "indicators/indicators.hpp"
#include "logger/logger.hpp"

using namespace std;
using namespace indicators;
namespace po = boost::program_options;
namespace al = boost::algorithm;

static ndnc::face::Face *face;
static ndnc::benchmark::ft::Runner *client;
static std::vector<std::thread> workers;

void cleanOnExit() {
    if (client != nullptr) {
        client->stop();

        for (auto it = workers.begin(); it != workers.end(); ++it) {
            if (it->joinable()) {
                it->join();
            }
        }

        delete client;
    }

    if (face != nullptr) {
        delete face;
    }
}

void handler(sig_atomic_t signum) {
    cleanOnExit();
    exit(signum);
}

static void usage(ostream &os, const string &app,
                  const po::options_description &desc) {
    os << "Usage: " << app
       << " [options]\nNote: This application needs --file argument "
          "specified\n\n"
       << desc;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, handler);

    ndnc::benchmark::ft::ClientOptions opts;
    std::string pipelineType = "fixed";

    po::options_description description("Options", 120);
    description.add_options()("file", po::value<string>(&opts.file),
                              "The path to the file to be copied over NDN");
    description.add_options()(
        "gqlserver",
        po::value<string>(&opts.gqlserver)->default_value(opts.gqlserver),
        "The GraphQL server address");
    description.add_options()(
        "lifetime",
        po::value<ndn::time::milliseconds::rep>()->default_value(
            opts.lifetime.count()),
        "The Interest lifetime in milliseconds. Specify a positive integer");
    description.add_options()(
        "mtu", po::value<size_t>(&opts.mtu)->default_value(opts.mtu),
        "Dataroom size. Specify a positive integer between 64 and 9000");
    description.add_options()(
        "name-prefix",
        po::value<string>(&opts.namePrefix)->default_value(opts.namePrefix),
        "The NDN Name prefix this consumer application publishes its "
        "Interest packets. Specify a non-empty string");
    description.add_options()(
        "nthreads",
        po::value<uint16_t>(&opts.nthreads)
            ->default_value(opts.nthreads)
            ->implicit_value(opts.nthreads),
        "The number of worker threads. Half will request the Interest packets "
        "and half will process the Data packets");
    description.add_options()(
        "pipeline-type",
        po::value<string>(&pipelineType)->default_value(pipelineType),
        "The pipeline type. Available options: fixed, aimd");
    description.add_options()("pipeline-size",
                              po::value<uint16_t>(&opts.pipelineSize)
                                  ->default_value(opts.pipelineSize),
                              "The maximum pipeline size for `fixed` type or "
                              "the initial ssthresh for `aimd` type");

    description.add_options()("help,h", "Print this help message and exit");

    po::variables_map vm;
    try {
        po::store(
            po::command_line_parser(argc, argv).options(description).run(), vm);
        po::notify(vm);
    } catch (const po::error &e) {
        cerr << "ERROR: " << e.what() << "\n";
        return 2;
    } catch (const boost::bad_any_cast &e) {
        cerr << "ERROR: " << e.what() << "\n";
        return 2;
    }

    const string app = argv[0];
    if (vm.count("help") > 0) {
        usage(cout, app, description);
        return 0;
    }

    if (vm.count("mtu") > 0) {
        if (opts.mtu < 64 || opts.mtu > 9000) {
            cerr << "ERROR: invalid MTU size\n\n";
            usage(cout, app, description);
            return 2;
        }
    }

    if (vm.count("gqlserver") > 0) {
        if (opts.gqlserver.empty()) {
            cerr << "ERROR: empty gqlserver argument value\n\n";
            usage(cout, app, description);
            return 2;
        }
    }

    if (vm.count("lifetime") > 0) {
        opts.lifetime = ndn::time::milliseconds(
            vm["lifetime"].as<ndn::time::milliseconds::rep>());
    }

    if (opts.lifetime < ndn::time::milliseconds{0}) {
        cerr << "ERROR: negative lifetime argument value\n\n";
        usage(cout, app, description);
        return 2;
    }

    if (vm.count("file") == 0) {
        cerr << "ERROR: no file path specified\n\n";
        usage(cerr, app, description);
        return 2;
    }

    if (opts.file.empty()) {
        cerr << "\nERROR: the file path argument cannot be an empty string\n\n";
        return 2;
    }

    if (al::to_lower_copy(pipelineType).compare("fixed") == 0) {
        opts.pipelineType = ndnc::PipelineType::fixed;
    } else if (al::to_lower_copy(pipelineType).compare("aimd") == 0) {
        opts.pipelineType = ndnc::PipelineType::aimd;
    } else {
        opts.pipelineType = ndnc::PipelineType::invalid;
    }

    if (opts.pipelineType == ndnc::PipelineType::invalid) {
        cerr << "ERROR: invalid pipeline type\n\n";
        usage(cout, app, description);
        return 2;
    }

    if (vm.count("name-prefix") > 0) {
        if (opts.namePrefix.empty()) {
            cerr << "ERROR: empty name prefix value\n\n";
            usage(cout, app, description);
            return 2;
        }
    }

    opts.nthreads = opts.nthreads % 2 == 1 ? opts.nthreads + 1 : opts.nthreads;

    // Open face
    face = new ndnc::face::Face();
    if (!face->connect(opts.mtu, opts.gqlserver, "ndncft-client")) {
        return 2;
    }

    client = new ndnc::benchmark::ft::Runner(*face, opts);

    ndnc::FileMetadata metadata{};
    if (!client->getFileMetadata(opts.file, metadata)) {
        cleanOnExit();
        return -2;
    }

    if (!metadata.isFile() || metadata.getFileSize() == 0) {
        cleanOnExit();
        return -2;
    }

    BlockProgressBar bar{option::BarWidth{80},
                         option::PrefixText{"Downloading "},
                         option::ShowPercentage{true},
                         option::ShowElapsedTime{true},
                         option::ShowRemainingTime{true},
                         option::ForegroundColor{Color::white},
                         option::MaxProgress{metadata.getFileSize()}};

    DynamicProgress<BlockProgressBar> bars(bar);

    show_console_cursor(true);
    // Do not hide bars when completed
    bars.set_option(option::HideBarWhenComplete{false});

    std::atomic<uint64_t> currentByteCount = 0;
    std::atomic<uint64_t> currentSegmentsCount = 0;

    auto receiveWorker = [&](int wid) {
        client->receiveFileContent(
            [&](uint64_t bytes) {
                if (bars[0].is_completed()) {
                    return;
                }

                currentByteCount += bytes;

                bars[0].set_progress(currentByteCount);
                bars[0].set_option(
                    option::PostfixText{to_string(currentByteCount) + "/" +
                                        to_string(metadata.getFileSize())});
                bars[0].tick();

                if (currentByteCount == metadata.getFileSize()) {
                    bars[0].set_option(
                        option::PrefixText{"Download complete "});
                    bars[0].mark_as_completed();
                }
            },
            std::ref(currentSegmentsCount), metadata.getFinalBlockID());
    };

    auto requestWorker = [&](int wid) {
        client->requestFileContent(wid, opts.nthreads / 2,
                                   metadata.getFinalBlockID(),
                                   metadata.getVersionedName());
    };

    for (int i = 0; i < opts.nthreads / 2; ++i) {
        workers.push_back(std::thread(receiveWorker, i));
        workers.push_back(std::thread(requestWorker, i));
    }

    auto start = std::chrono::high_resolution_clock::now();

    for (auto it = workers.begin(); it != workers.end(); ++it) {
        if (it->joinable()) {
            it->join();
        }
    }

    auto end = std::chrono::high_resolution_clock::now();

    auto duration = end - start;
    double goodput = ((double)metadata.getFileSize() * 8.0) /
                     (duration / std::chrono::nanoseconds(1)) * 1e9;

    cout << "\n--- statistics --\n"
         << client->readCounters()->nTxPackets
         << " interest packets transmitted, "
         << client->readCounters()->nRxPackets << " data packets received, "
         << client->readCounters()->nTimeouts
         << " packets retransmitted on timeout\n"
         << "average delay: " << client->readCounters()->averageDelay() << "\n"
         << "goodput: " << binaryPrefix(goodput) << "bit/s"
         << "\n\n";

    cleanOnExit();
    return 0;
}

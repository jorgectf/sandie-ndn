/******************************************************************************
 * Named Data Networking plugin for xrootd                                    *
 * Copyright © 2018 California Institute of Technology                        *
 *                                                                            *
 * Author: Catalin Iordache <catalin.iordache@cern.ch>                        *
 *                                                                            *
 * This program is free software: you can redistribute it and/or modify       *
 * it under the terms of the GNU General Public License as published by       *
 * the Free Software Foundation, either version 3 of the License, or          *
 * (at your option) any later version.                                        *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.     *
 *****************************************************************************/

#include <fstream>
#include <iostream>
#include <stdlib.h>

#include <ndn-cxx/version.hpp>

#include <boost/filesystem.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/version.hpp>

#include "../common/xrdndn-logger.hh"
#include "xrdndn-consumer-options.hh"
#include "xrdndn-consumer-version.hh"
#include "xrdndn-consumer.hh"

namespace xrdndnconsumer {

struct CommandLineOptions {
    std::string outfile;

    uint64_t bsize = 262144;
    uint16_t nthreads = 1;
};

class SynchronizedWrite {
  public:
    SynchronizedWrite(std::string path)
        : ostream(path, std::ofstream::binary) {}
    ~SynchronizedWrite() { ostream.close(); }

    void write(off_t offset, const char *buf, off_t len) {
        boost::lock_guard<boost::mutex> lock(mutex_);
        ostream.seekp(offset);
        ostream.write(buf, len);
    }

  private:
    std::ofstream ostream;
    boost::mutex mutex_;
};

struct CommandLineOptions cmdLineOpts;
struct Options consumerOpts;

std::shared_ptr<SynchronizedWrite> syncWrite;
std::shared_ptr<Consumer> consumer;

void read(off_t fileSize, off_t off, int threadID) {
    std::string buff(cmdLineOpts.bsize, '\0');
    off_t blen, offset;
    ssize_t retRead = 0;
    std::map<uint64_t, std::pair<std::string, uint64_t>> contentStore;
    offset = off;
    do {
        if (offset >= fileSize) {
            break;
        }

        if (offset + static_cast<off_t>(cmdLineOpts.bsize) > fileSize)
            blen = fileSize - offset;
        else
            blen = cmdLineOpts.bsize;

        NDN_LOG_TRACE("[Thread " << threadID << "] Reading " << blen << "@"
                                 << offset);

        retRead = consumer->Read(&buff[0], offset, blen);
        contentStore[offset] = std::make_pair(std::string(buff), retRead);

        offset += cmdLineOpts.bsize * cmdLineOpts.nthreads;
    } while (retRead > 0);

    if (!cmdLineOpts.outfile.empty()) {
        for (auto it = contentStore.begin(); it != contentStore.end();
             it = contentStore.erase(it))
            syncWrite->write(it->first, it->second.first.data(),
                             it->second.second);
    }
}

int copyFile() {
    int ret = consumer->Open();
    if (ret != XRDNDN_ESUCCESS) {
        NDN_LOG_ERROR("Unable to open file: " << consumerOpts.path << ". "
                                              << strerror(abs(ret)));
        return 2;
    }

    struct stat info;
    ret = consumer->Fstat(&info);
    if (ret != XRDNDN_ESUCCESS) {
        NDN_LOG_ERROR("Unable to get fstat for file: "
                      << consumerOpts.path << ". " << strerror(abs(ret)));
        return 2;
    }

    if (!cmdLineOpts.outfile.empty()) {
        syncWrite = std::make_shared<SynchronizedWrite>(cmdLineOpts.outfile);
    }
    boost::thread_group threads;

    for (auto i = 0; i < cmdLineOpts.nthreads; ++i) {
        threads.create_thread(
            std::bind(read, info.st_size, cmdLineOpts.bsize * i, i));
    }

    threads.join_all();
    consumer->Close();
    return 0;
}

int run() { return copyFile(); }

static void usage(std::ostream &os, const std::string &programName,
                  const boost::program_options::options_description &desc) {
    os << "Usage: " << programName
       << " [options]\nNote: This application needs --input-file argument "
          "specified\n\n"
       << desc;
}

static void info() {
    NDN_LOG_INFO(
        "\nThe NDN Consumer used in the NDN based filesystem plugin for "
        "XRootD.\nDeveloped by Caltech@CMS.\n");
}

int main(int argc, char **argv) {
    std::string programName = argv[0];
    std::string logLevel("INFO");

    boost::program_options::options_description description("Options", 120);
    description.add_options()(
        "bsize",
        boost::program_options::value<uint64_t>(&cmdLineOpts.bsize)
            ->default_value(cmdLineOpts.bsize)
            ->implicit_value(cmdLineOpts.bsize),
        "Read buffer size in bytes. Specify any value between 8KB and 1GB in "
        "bytes")("help,h", "Print this help message and exit")(
        "input-file",
        boost::program_options::value<std::string>(&consumerOpts.path),
        "Path to file to be copied over Name Data Networking")(
        "log-level",
        boost::program_options::value<std::string>(&logLevel)
            ->default_value(logLevel)
            ->implicit_value("NONE"),
        "Log level. Available options: TRACE, DEBUG, INFO, WARN, ERROR, "
        "FATAL. More information can be found at "
        "https://named-data.net/doc/ndn-cxx/current/manpages/ndn-log.html")(
        "nthreads",
        boost::program_options::value<uint16_t>(&cmdLineOpts.nthreads)
            ->default_value(cmdLineOpts.nthreads)
            ->implicit_value(cmdLineOpts.nthreads),
        "Number of threads to read the file concurrently")(
        "output-file",
        boost::program_options::value<std::string>(&cmdLineOpts.outfile)
            ->default_value("")
            ->implicit_value("./ndnfile.out"),
        "Path to output file copied over Name Data Networking")(
        "pipeline-size",
        boost::program_options::value<size_t>(&consumerOpts.pipelineSize)
            ->default_value(consumerOpts.pipelineSize)
            ->implicit_value(consumerOpts.pipelineSize),
        "The number of concurrent Interest packets expressed at one time in "
        "the fixed window size Pipeline. Specify any value between 1 and 512")(
        "version,V", "Show version information and exit");

    boost::program_options::variables_map vm;
    try {
        boost::program_options::store(
            boost::program_options::command_line_parser(argc, argv)
                .options(description)
                .run(),
            vm);
        boost::program_options::notify(vm);
    } catch (const boost::program_options::error &e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 2;
    } catch (const boost::bad_any_cast &e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 2;
    }

    if (vm.count("help") > 0) {
        usage(std::cout, programName, description);
        return 0;
    }

    if (vm.count("version") > 0) {
        std::cout << XRDNDN_CONSUMER_VERSION_STRING << std::endl;
        return 0;
    }

    if (vm.count("bsize") > 0) {
        if (cmdLineOpts.bsize < 1024 || cmdLineOpts.bsize > 1073741824) {
            std::cerr << "Buffer size must be between 8KB and 1GB."
                      << std::endl;
            return 2;
        }
    }

    if (vm.count("input-file") == 0) {
        std::cerr << "ERROR: Specify file to be copied over NDN" << std::endl;
        usage(std::cerr, programName, description);
        return 2;
    }

    if (vm.count("pipeline-size") > 0) {
        if (consumerOpts.pipelineSize < 1 || consumerOpts.pipelineSize > 512) {
            std::cerr << "ERROR: Pipeline size must be between 1 and 512"
                      << std::endl;
            return 2;
        }
    }

    if (boost::filesystem::exists(boost::filesystem::path(consumerOpts.path))) {
        consumerOpts.path =
            boost::filesystem::canonical(consumerOpts.path,
                                         boost::filesystem::current_path())
                .string();
    }

    try {
        ndn::util::Logging::setLevel(CONSUMER_LOGGER_PREFIX "=" + logLevel);
    } catch (const std::invalid_argument &e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        usage(std::cerr, programName, description);
        return 2;
    }

    if (!cmdLineOpts.outfile.empty())
        cmdLineOpts.outfile =
            boost::filesystem::path(cmdLineOpts.outfile).string();

    info();

    NDN_LOG_INFO("XRootD NDN Consumer version " XRDNDN_CONSUMER_VERSION_STRING
                 " starting");
    {
        const std::string boostBuildInfo =
            "Boost version " + std::to_string(BOOST_VERSION / 100000) + "." +
            std::to_string(BOOST_VERSION / 100 % 1000) + "." +
            std::to_string(BOOST_VERSION % 100);

        const std::string ndnCxxInfo =
            "ndn-cxx version " NDN_CXX_VERSION_STRING;

        NDN_LOG_INFO(
            "xrdndn-consumer build " XRDNDN_CONSUMER_VERSION_BUILD_STRING
            " built with " BOOST_COMPILER ", with " BOOST_STDLIB ", with "
            << boostBuildInfo << ", with " << ndnCxxInfo);

        NDN_LOG_INFO(
            "Selected Options: Read buffer size: "
            << cmdLineOpts.bsize
            << "B, Pipeline Size: " << consumerOpts.pipelineSize
            << "Interests, Input file: " << consumerOpts.path
            << ", Output file: "
            << (cmdLineOpts.outfile.empty() ? "N/D" : cmdLineOpts.outfile));
    }

    consumer = Consumer::getXrdNdnConsumerInstance(consumerOpts);
    if (!consumer) {
        NDN_LOG_ERROR(
            "Could not get xrdndnd consumer instance"); // TODO: prettify use
                                                        // std::cout
        return 2;
    }
    return run();
}
} // namespace xrdndnconsumer

int main(int argc, char **argv) { return xrdndnconsumer::main(argc, argv); }
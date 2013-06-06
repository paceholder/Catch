/*
 *  Created by Phil on 31/10/2010.
 *  Copyright 2010 Two Blue Cubes Ltd. All rights reserved.
 *
 *  Distributed under the Boost Software License, Version 1.0. (See accompanying
 *  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 */
#ifndef TWOBLUECUBES_CATCH_RUNNER_HPP_INCLUDED
#define TWOBLUECUBES_CATCH_RUNNER_HPP_INCLUDED

#include "internal/catch_commandline.hpp"
#include "internal/catch_list.hpp"
#include "internal/catch_runner_impl.hpp"
#include "internal/catch_test_spec.h"
#include "internal/catch_version.h"
#include "internal/catch_text.h"

#include <fstream>
#include <stdlib.h>
#include <limits>

namespace Catch {

    class Runner {

    public:
        Runner( Ptr<Config> const& config )
        :   m_config( config )
        {
            openStream();
            makeReporter();
        }

        Totals runTests() {

            std::vector<TestCaseFilters> filterGroups = m_config->filters();
            if( filterGroups.empty() ) {
                TestCaseFilters filterGroup( "" );
                filterGroups.push_back( filterGroup );
            }

            RunContext context( m_config.get(), m_reporter );

            Totals totals;

            for( std::size_t i=0; i < filterGroups.size() && !context.aborting(); ++i ) {
                context.testGroupStarting( filterGroups[i].getName(), i, filterGroups.size() );
                totals += runTestsForGroup( context, filterGroups[i] );
                context.testGroupEnded( filterGroups[i].getName(), totals, i, filterGroups.size() );
            }
            return totals;
        }

        Totals runTestsForGroup( RunContext& context, const TestCaseFilters& filterGroup ) {
            Totals totals;
            std::vector<TestCase>::const_iterator it = getRegistryHub().getTestCaseRegistry().getAllTests().begin();
            std::vector<TestCase>::const_iterator itEnd = getRegistryHub().getTestCaseRegistry().getAllTests().end();
            int testsRunForGroup = 0;
            for(; it != itEnd; ++it ) {
                if( filterGroup.shouldInclude( *it ) ) {
                    testsRunForGroup++;
                    if( m_testsAlreadyRun.find( *it ) == m_testsAlreadyRun.end() ) {

                        if( context.aborting() )
                            break;

                        totals += context.runTest( *it );
                        m_testsAlreadyRun.insert( *it );
                    }
                }
            }
            if( testsRunForGroup == 0 && !filterGroup.getName().empty() )
                m_reporter->noMatchingTestCases( filterGroup.getName() );
            return totals;
            
        }

    private:
        void openStream() {
            // Open output file, if specified
            if( !m_config->getFilename().empty() ) {
                m_ofs.open( m_config->getFilename().c_str() );
                if( m_ofs.fail() ) {
                    std::ostringstream oss;
                    oss << "Unable to open file: '" << m_config->getFilename() << "'";
                    throw std::domain_error( oss.str() );
                }
                m_config->setStreamBuf( m_ofs.rdbuf() );
            }
        }
        void makeReporter() {
            std::string reporterName = m_config->getReporterName().empty()
                ? "console"
                : m_config->getReporterName();

            m_reporter = getRegistryHub().getReporterRegistry().create( reporterName, m_config.get() );
            if( !m_reporter ) {
                std::ostringstream oss;
                oss << "No reporter registered with name: '" << reporterName << "'";
                throw std::domain_error( oss.str() );
            }
        }
        
    private:
        Ptr<Config> m_config;
        std::ofstream m_ofs;
        Ptr<IStreamingReporter> m_reporter;
        std::set<TestCase> m_testsAlreadyRun;
    };

    inline void showHelp( std::string const& processName ) {
        Clara::CommandLine<ConfigData> cli = makeCommandLineParser();

        std::cout << "\nCatch v"    << libraryVersion.majorVersion << "."
                                    << libraryVersion.minorVersion << " build "
                                    << libraryVersion.buildNumber;
        if( libraryVersion.branchName != "master" )
            std::cout << " (" << libraryVersion.branchName << " branch)";
        std::cout << "\n";

        cli.usage( std::cout, processName );
        std::cout << "\nFor more detail usage please see: https://github.com/philsquared/Catch/wiki/Command-line\n" << std::endl;
    }

    class Session {
        static bool alreadyInstantiated;
        
    public:
    
        struct OnUnusedOptions { enum DoWhat { Ignore, Fail }; };

        Session()
        : cli( makeCommandLineParser() ) {
            if( alreadyInstantiated ) {
                std::string msg = "Only one instance of Catch::Session can ever be used";
                std::cerr << msg << std::endl;
                throw std::logic_error( msg );
            }
            alreadyInstantiated = true;
        }
        ~Session() {
            Catch::cleanUp();
        }

        int applyCommandLine( int argc, char* const argv[], OnUnusedOptions::DoWhat unusedOptionBehaviour = OnUnusedOptions::Fail ) {
            try {
                unusedTokens = cli.parseInto( argc, argv, configData );
                if( unusedOptionBehaviour == OnUnusedOptions::Fail )
                    enforceNoUsedTokens();
                if( configData.showHelp )
                    showHelp( configData.processName );
                config.reset();
            }
            catch( std::exception& ex ) {
                std::cerr   << "\nError in input:\n"
                            << "  " << ex.what() << "\n\n";
                cli.usage( std::cout, configData.processName );
                return (std::numeric_limits<int>::max)();
            }
            return 0;
        }

        void useConfigData( ConfigData const& _configData ) {
            configData = _configData;
            config.reset();
        }

        void enforceNoUsedTokens() const {
            if( !unusedTokens.empty() ) {
                std::vector<Clara::Parser::Token>::const_iterator
                    it = unusedTokens.begin(),
                    itEnd = unusedTokens.end();
                std::string msg;
                for(; it != itEnd; ++it )
                    msg += "  unrecognised option: " + it->data + "\n";
                throw std::runtime_error( msg.substr( 0, msg.size()-1 ) );
            }
        }
        
        int run( int argc, char* const argv[] ) {

            int returnCode = applyCommandLine( argc, argv );
            if( returnCode == 0 )
                returnCode = run();
            return returnCode;
        }

        int run() {
            if( configData.showHelp )
                return 0;

            try
            {
                if( !config )
                    config = new Config( configData );
                Runner runner( config );

                // Handle list request
                if( list( config ) )
                    return 0;

                return static_cast<int>( runner.runTests().assertions.failed );
            }
            catch( std::exception& ex ) {
                std::cerr << ex.what() << std::endl;
                return (std::numeric_limits<int>::max)();
            }
        }
        
        private:
            Clara::CommandLine<ConfigData> cli;
            std::vector<Clara::Parser::Token> unusedTokens;
            ConfigData configData;
            Ptr<Config> config;
    };

    bool Session::alreadyInstantiated = false;
    
} // end namespace Catch

#endif // TWOBLUECUBES_CATCH_RUNNER_HPP_INCLUDED

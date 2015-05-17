// Xerus - A General Purpose Tensor Library
// Copyright (C) 2014-2015 Benjamin Huber and Sebastian Wolf. 
// 
// Xerus is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published
// by the Free Software Foundation, either version 3 of the License,
// or (at your option) any later version.
// 
// Xerus is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU Affero General Public License for more details.
// 
// You should have received a copy of the GNU Affero General Public License
// along with Xerus. If not, see <http://www.gnu.org/licenses/>.
//
// For further information on Xerus visit https://libXerus.org 
// or contact us at contact@libXerus.org.

#ifdef TEST_

    #include <iostream>
    #include <iomanip>
    #include <chrono>
    #include <signal.h>
    #include <sstream>

    #include <string.h> // for strsignal
    #include <sys/stat.h>
    
    #include "../../../include/xerus/misc/standard.h"
    #include "../../../include/xerus/misc/exceptions.h"
    

    #include "../../../include/xerus/misc/testManager.h"
    #include "../../../include/xerus/misc/stringUtilities.h"

    std::map<std::string, std::map<std::string, std::function<bool ()>>> *___UnitTest::tests;
	std::map<___RequiredTest::identifier, size_t> *___RequiredTest::tests;

    bool ___test(const std::pair<std::string, std::function<bool ()>> &_t) {
        bool passed = false;
        
        std::cout << "| " << _t.first << " starting: "  << std::flush;
        
        std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
        try {
            passed = _t.second(); // executes the test
        } catch (const MISC::generic_error &e) {
            std::cout << u8"\033[1;31m\u2717 \033[0m" << std::endl;
            std::cerr << "| Test has thrown an uncaught xerus::generic_error():" << std::endl;
            std::cerr << e.what() << std::endl;
            passed = false;
        } catch (const std::exception &e) {
            std::cout << u8"\033[1;31m\u2717 \033[0m" << std::endl;
            std::cerr << "| Test has thrown an uncaught std::exception:" << std::endl;
            std::cerr << e.what() << std::endl;
            passed = false;
        } catch (...) {
            std::cout << u8"\033[1;31m\u2717 \033[0m" << std::endl;
            std::cerr << "| Test has thrown an uncaught unknown exception..." << std::endl;
            passed = false;
        }
        std::chrono::microseconds::rep time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start).count();
        
        if (passed) { 
            std::cout << std::endl << "| " << _t.first << ":\033[1;32m passed!\033[0m (" << std::fixed << std::setprecision(3) << (double)time/1000.0 << " ms)" << std::endl << "| " << std::endl;
        } else {
            std::cout << std::endl << "| " << _t.first << ":\033[1;31m FAILED!\033[0m (" << std::fixed << std::setprecision(3) << (double)time/1000.0 << " ms)" << std::endl << "| " << std::endl;
        }
        
        return passed;
    }

    void ___print_group_name(std::string _g) {
        int a = (77-(int)_g.size())/2; if (a<0) a=0;
        int b = 77-(77+(int)_g.size())/2; if (b<0) b=0;
        std::cout << "-------------------------------------------------------------------------------" << std::endl;
        std::cout << "|" << std::string((size_t)a, ' ') << "\033[1m" << _g << "\033[0m" << std::string((size_t)b, ' ')  << ' ' << std::endl;
        std::cout << "|" << std::endl;
        //std::cout << "-------------------------------------------------------------------------------" << std::endl;
    }

    void ___print_group_summary(std::string _g, unsigned _passes, unsigned _total) {
        std::stringstream ts;
        ts.str(""); ts.clear();
        ts << _g << " summary " << (_passes == _total?"\033[1;32m":"\033[1;31m") << _passes << " of " << _total << " passed\033[0m";
        int a = (77-((int)ts.str().size()-11))/2; if (a<0) a=0;
        int b = 77-(77+((int)ts.str().size()-11))/2; if (b<0) b=0;
        //std::cout << "-------------------------------------------------------------------------------" << std::endl;
        std::cout << "|" << std::endl;
        std::cout << "|" << std::string((size_t)a, ' ') << ts.str() << std::string((size_t)b, ' ')  << ' ' << std::endl;
        std::cout << "-------------------------------------------------------------------------------" << std::endl;
    }

    _noreturn_ void ___catch_signals(int _sig)  {
        XERUS_THROW(MISC::generic_error() << "signal " << _sig << " = " << strsignal(_sig) << "callstack:\n" << MISC::get_call_stack());
    }

    typedef void (*required_test_t)(void);
    
    #undef main
    int main(int argc, char* argv[]) {
    // 	signal(SIGINT, ___catch_signals); // users ctrl+c should actually terminate the program
    // 	signal(SIGTERM, ___catch_signals);
    // 	signal(SIGHUP, ___catch_signals);
        //signal(SIGABRT,___catch_signals); // common source of abort is "double free or corurption" in which case we cannot continue
        signal(SIGFPE,___catch_signals);
        signal(SIGILL,___catch_signals);
        signal(SIGSEGV,___catch_signals);
        
		// perform required_test initializations
		// pass address of ___catch_signals as the address of main cannot be taken as by ISO c++...
		std::pair<uintptr_t, uintptr_t> requiredTestRange = MISC::get_range_of_section(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&___catch_signals)), "required_tests");
		for (required_test_t *p = (required_test_t *)requiredTestRange.first; p < (required_test_t *)requiredTestRange.second; p += 1) {
			try {
				(*p)();
			} catch (...) {
				std::cout << "required test initialization failed. required test listing might will be wrong." << std::endl;
				break;
			}
		}
		
        //Calculate complete time
        std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
        
        std::cout << "###############################################################################" << std::endl;
        std::cout << "#                                unit-testing                                 #" << std::endl;
        std::cout << "###############################################################################" << std::endl;
		// no unittests defined (ie. the map tests does not exist!)
		if (!___UnitTest::tests) {
			std::cout << "no unittests defined." << std::endl;
			std::cout << "use the macro UNIT_TEST(group, testname, ...) to define unittests inside the sourcecode." << std::endl;
			return 0;
		}
		
        if (argc < 2) {
            std::cout << "usage:" << std::endl;
            std::cout << "  " << MISC_NAMESPACE::explode(argv[0],'/').back() << " [groupname] ..." << std::endl;
            std::cout << "  " << MISC_NAMESPACE::explode(argv[0],'/').back() << " [groupname]:[testname] ..." << std::endl;
            std::cout << "  " << MISC_NAMESPACE::explode(argv[0],'/').back() << " all" << std::endl << std::endl;
            std::cout << "available groups:" << std::endl;
            for (const auto &p : *___UnitTest::tests) {
                std::cout << "# " <<  p.first << std::endl;
            }
            return 0;
        }
        
        unsigned passCount=0;
        unsigned totalPassCount=0; 
        unsigned totalCount=0;
        
        for (int currArg = 1; currArg < argc; ++currArg) {
            std::string grp = argv[currArg];
            // do all unit tests
            if (grp == "all") {
                for (const auto &p : *___UnitTest::tests) {
                    ___print_group_name(p.first);
                    passCount=0;
                    for (const auto &t : p.second) {
                        totalCount += 1;
                        if (___test(t)) {
                            passCount += 1;
                            totalPassCount += 1;
                        }
                    }
                    ___print_group_summary(p.first, passCount, (unsigned)p.second.size());
                }
                break;
            }
            // explicit test inside a group?
            std::vector<std::string> cmd = MISC_NAMESPACE::explode(grp,':');
            if (cmd.size()>1) {
				if (cmd.size()>2) {
                    std::cout << "########## \033[1;31munknown syntax '" << grp << "'\033[0m" << std::endl;
                    continue;
                }
                if (!___UnitTest::tests->count(cmd[0]) || (*___UnitTest::tests)[cmd[0]].count(cmd[1]) == 0) {
                    std::cout << "########## \033[1;31munknown unittest '" << cmd[0] << ":" << cmd[1] << "'\033[0m" << std::endl;
                    continue;
                }
                totalCount += 1;
                if (___test({grp, (*___UnitTest::tests)[cmd[0]][cmd[1]]}) ) {
                    totalPassCount += 1;
                }
            } else {
                // unknown group
                if (___UnitTest::tests->count(grp) == 0) {
                    std::cout << "########## \033[1;31munknown group or unittest '" << grp << "'\033[0m" << std::endl;
                    continue;
                }
                // one (whole) group
                ___print_group_name(grp);
                passCount=0; 
                for (const auto &t : (*___UnitTest::tests)[grp]) {
                    totalCount += 1;
                    if (___test(t)) {
                        passCount += 1;
                        totalPassCount += 1;
                    }
                }
                ___print_group_summary(grp, passCount, (unsigned)(*___UnitTest::tests)[grp].size());
            }
        }
        
        //Calc total elapsed time
        std::chrono::microseconds::rep totalTime = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - startTime).count();
        
        ___print_group_summary("total", totalPassCount, totalCount);
        
        std::cout << "|" << std::endl;
        std::cout << "|" << std::string(23, ' ') << "Total time elapsed: " << (double)totalTime/1000.0 << " ms" << std::string(50, ' ')  << ' ' << std::endl;
        std::cout << "-------------------------------------------------------------------------------" << std::endl;
		
#ifdef TEST_COVERAGE_
		// check whether all REQUIRED_TESTs were tested
		std::map<std::string, std::pair<size_t, size_t>> perFile;
		
		if (___RequiredTest::tests) {
			for (auto &t : (*___RequiredTest::tests)) {
	// 			std::cout << MISC::demangle_cxa(t.first.functionName) << " (" << t.first.filename << ":" << t.first.lineNumber << ")" << t.second << std::endl;
				std::string normPath = MISC::normalize_pathname(t.first.filename);
				std::pair<size_t, size_t> &pf = perFile[normPath];
				pf.second += 1;
				if (t.second == 0) {
					std::cout << "\033[1;31m missing test for function \033[0m" 
						<< MISC::demangle_cxa(t.first.functionName) << " (" << normPath << ":" << t.first.lineNumber << ")" << std::endl;
				} else {
					pf.first += 1;
				}
			}
			
			for (auto &f : perFile) {
				std::pair<size_t, size_t> &fstats = f.second;
				if (fstats.first == fstats.second) {
					std::cout << "file " << f.first << " :\033[1;32m " << fstats.first << " of " << fstats.second << " tests performed\033[0m" << std::endl;
				} else {
					std::cout << "file " << f.first << " :\033[1;31m " << fstats.first << " of " << fstats.second << " tests performed\033[0m" << std::endl;
				}
			}
		}
#endif
		// destroy all stored tests to make memory-leak detection simpler
		delete ___UnitTest::tests;
		delete ___RequiredTest::tests;
        
        return totalPassCount != totalCount;
    }

    #define main(...) ___horst_main_will_not_be_called( __VA_ARGS__ )
#endif
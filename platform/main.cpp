///
/// @file
/// @brief eForth main program for testing on Desktop PC (Linux and Cygwin)
///
#include <iostream>      // cin, cout
#include <fstream>       // ifstream
#include <cstdint>
#include <sys/sysinfo.h>

using namespace std;

extern void forth_init();
extern int  forth_vm(const char *cmd, void(*)(int, const char*)=NULL);
extern void forth_teardown();

const char* APP_VERSION = "xeForth v1.0";
///====================================================================
///
///> Memory statistics - for heap, stack, external memory debugging
///
typedef uint64_t U64;
void mem_stat() {
    cout << APP_VERSION;

#if (ARDUINO || ESP32)
    int64_t memsize;
    size_t len = sizeof(memsize);
    if (sysctlbyname("hw.memsize", &memsize, &len, NULL, 0) == 0) {
      cout << ", RAM " << (memsize >> 20) << " MB";
    }
#else // !(ARDUINO || ESP32) i.e. Linux
    struct sysinfo si;
    if (sysinfo(&si) != -1) {
      U64 f = (U64)si.freeram * si.mem_unit;
      U64 t = (U64)si.totalram * si.mem_unit;
      U64 p = f * 1000L / t;
      cout << ", RAM " << static_cast<float>(p) * 0.1 << "% free (" << (f >> 20)
           << " / " << (t >> 20) << " MB)";
    }
#endif // (ARDUINO || ESP32)

    cout << endl;
}
///
///> include external Forth script
///
void outer(istream &in) {
    string cmd;                               ///< input command; TODO: static pool
    while (getline(in, cmd)) {                ///> fetch user input
        // printf("cmd=<%s>\n", cmd.c_str());
        if (forth_vm(cmd.c_str())) break;     ///> run outer interpreter (single task)
    }
}
void forth_include(const char *fn) {
    ifstream ifile(fn);                       ///< open input stream
    if (ifile.is_open()) {
        outer(ifile);
    }
    else cout << "file " << fn << " open failed!";

    ifile.close();
}
///====================================================================
///
/// main program - Note: Arduino and ESP32 have their own main-loop
///
int main(int ac, char* av[]) {
    forth_init();                             ///> initialize dictionary
    
    mem_stat();                               ///> show memory status
    srand((int)time(0));                      ///> seed random generator
    outer(cin);                               ///> Forth outer interpreter
    
    forth_teardown();                         ///> clean up before we go
    cout << APP_VERSION << " Done!" << endl;
    return 0;
}
///====================================================================

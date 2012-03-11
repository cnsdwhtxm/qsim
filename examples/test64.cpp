/*****************************************************************************\
* Qemu Simulation Framework (qsim)                                            *
* Qsim is a modified version of the Qemu emulator (www.qemu.org), coupled     *
* a C++ API, for the use of computer architecture researchers.                *
*                                                                             *
* This work is licensed under the terms of the GNU GPL, version 2. See the    *
* COPYING file in the top-level directory.                                    *
\*****************************************************************************/
#include <iostream>
#include <fstream>
#include <iomanip>

#include "distorm.h"

#include <qsim.h>

using namespace std;
using Qsim::OSDomain;

const uint64_t ioapic_base = 0xa0000;

map<uint64_t, string> sysmap;

void read_sys_map(const char* mapfile) {
  ifstream sysmap_stream;
  string   t;
  uint64_t addr;
  string   sym;
  
  sysmap_stream.open(mapfile);
  
  for (;;) {
    sysmap_stream >> std::hex >> addr >> t >> sym;
    if (!sysmap_stream) break;
    sysmap[addr] = sym;
    //cout << "Added 0x" << std::hex << addr << ':' << sym << " to sysmap.\n";  
  }
  
  sysmap_stream.close();
}

void mk_checksum(OSDomain &osd, uint64_t addr, size_t sumbyte, size_t size) {
  int sum = 0;

  for (size_t i = 0; i < size; ++i) {
    if (i != sumbyte) {
      signed char b;
      osd.mem_rd(b, addr + i);
      sum += b;
      std::cout << std::hex << std::setw(2) << std::setfill('0') << (unsigned)b << ' ';
    }
  }

  unsigned char checksum( (-sum)&0xff );
  osd.mem_wr(checksum, addr + sumbyte);
  std::cout << "\nChecksum: " << std::hex << std::setw(2) << std::setfill('0') << (unsigned)checksum << '\n';
}

// Make an SFI table using dirty type punning.
void mk_sfi_table(OSDomain &osd) {
  // SFI Table to place at 0xe0000 ()
  unsigned n = osd.get_n();

  const char *sys_ident = "SYST", *cpu_ident="CPUS", *apic_ident="APIC",
             *vendor_str = "qsim00";

  uint64_t base = 0xe0000, cput_base = 0xe0030,
           apic_base = 0xe0030 + 24 + n/4*16;

  // SYST, the system table
  osd.mem_wr(*(uint32_t*)sys_ident, base);    // "SYST"
  osd.mem_wr((uint32_t)36, base+4);           // System table length.
  osd.mem_wr((uint8_t)1, base+8);             // SFI Revision

  for (unsigned i = 0; i < 6; ++i)            // Vendor ID
    osd.mem_wr(vendor_str[i], base+10+i);

  osd.mem_wr((uint64_t)0x00000000, base+16);  // OEM Table ID  
  osd.mem_wr(cput_base, base+24);             // Address of CPU table
  osd.mem_wr(apic_base, base+32);             // Address of APIC table

  mk_checksum(osd, base, 9, 40);

  // CPUS, the CPU table
  osd.mem_wr(*(uint32_t*)cpu_ident, cput_base);   // "CPUS"
  osd.mem_wr(uint32_t(24+4*n), cput_base+4);      // CPU table length.
  osd.mem_wr((uint8_t)1, cput_base+8);            // SFI Revision
  
  
  for (unsigned i = 0; i < 6; ++i)                // Vendor ID
    osd.mem_wr(vendor_str[i], cput_base+10+i);

  osd.mem_wr((uint64_t)0x00000000, cput_base+16); // OEM Table ID
  
  for (unsigned i = 0; i < n; ++i)                // LAPIC ID for each CPU
    osd.mem_wr((uint32_t)i, cput_base+24+4*i);

  mk_checksum(osd, cput_base, 9, 24+4*n);

  // APIC, the IO-APIC table
  osd.mem_wr(*(uint32_t*)apic_ident, apic_base);  // "APIC"
  osd.mem_wr((uint32_t)32, apic_base+4);          // IO-APIC table length.
  osd.mem_wr((uint8_t)1, apic_base+8);            // SFI Revision.

  for (unsigned i = 0; i < 6; ++i)                // Vendor ID
    osd.mem_wr(vendor_str[i], apic_base+10+i);

  osd.mem_wr((uint64_t)0x00000000, apic_base+16); // OEM Table ID

  osd.mem_wr((uint64_t)ioapic_base, apic_base+24);// IO-APIC base address.

  mk_checksum(osd, apic_base, 9, 32);

}

class TraceWriter {
public:
  TraceWriter(OSDomain &osd, ostream &tracefile) : 
    osd(osd), tracefile(tracefile), finished(false) 
  { 
    //osd.set_app_start_cb(this, &TraceWriter::app_start_cb); 
    osd.set_inst_cb(this, &TraceWriter::inst_cb);
    osd.set_io_cb(this, &TraceWriter::io_cb);
  }

  bool hasFinished() { return finished; }

  void app_start_cb(int c) {
    static bool ran = false;
    if (!ran) {
      ran = true;
      osd.set_inst_cb(this, &TraceWriter::inst_cb);
      osd.set_atomic_cb(this, &TraceWriter::atomic_cb);
      osd.set_mem_cb(this, &TraceWriter::mem_cb);
      osd.set_int_cb(this, &TraceWriter::int_cb);
      osd.set_io_cb(this, &TraceWriter::io_cb);
      osd.set_reg_cb(this, &TraceWriter::reg_cb);
      osd.set_app_end_cb(this, &TraceWriter::app_end_cb);
    }
  }

  void app_end_cb(int c)   { finished = true; }

  int atomic_cb(int c) {
    tracefile << std::dec << c << ": Atomic\n";
    return 0;
  }

  void inst_cb(int c, uint64_t v, uint64_t p, uint8_t l, const uint8_t *b, 
               enum inst_type t)
  {
    if (sysmap.find(v) != sysmap.end()) 
      cerr << "===" << sysmap[v] << "()=== - " << std::dec << c << '\n';

#if 0
    static int count = 0;
    if (++count != 100000000) return;
    else count = 0;
#endif
    _DecodedInst inst[15];
    unsigned int shouldBeOne;
    distorm_decode(0, b, l, Decode32Bits, inst, 15, &shouldBeOne);

#if 0
    tracefile << std::dec << c << ": Inst@(0x" << std::hex << v << "/0x" << p 
              << ", tid=" << std::dec << osd.get_tid(c) << ", "
              << ((osd.get_prot(c) == Qsim::OSDomain::PROT_USER)?"USR":"KRN")
              << (osd.idle(c)?"[IDLE]":"[ACTIVE]")
              << "): " << std::hex;

    //while (l--) tracefile << ' ' << std::setw(2) << std::setfill('0') 
    //                      << (unsigned)*(b++);

    if (shouldBeOne != 1) tracefile << "[Decoding Error]";
    else tracefile << inst[0].mnemonic.p << ' ' << inst[0].operands.p;

    tracefile << " (" << itype_str[t] << ")\n";
#endif
  }

  void mem_cb(int c, uint64_t v, uint64_t p, uint8_t s, int w) {
    tracefile << std::dec << c << ":  " << (w?"WR":"RD") << "(0x" << std::hex
              << v << "/0x" << p << "): " << std::dec << (unsigned)(s*8) 
              << " bits.\n";
  }

  int int_cb(int c, uint8_t v) {
    tracefile << std::dec << c << ": Interrupt 0x" << std::hex << std::setw(2)
              << std::setfill('0') << (unsigned)v << '\n';
    return 0;
  }

  void cmos_wr(uint8_t addr, uint8_t data) {
    std::cout << "Write to CMOS addr 0x" << std::hex << addr << ", 0x" 
              << data << '\n';
    switch(addr) {
    default:
      std::cout << "Unsupported CMOS address for write.\n";
      //exit(0);
    }
  }

  void cmos_rd(uint8_t addr) { 
    std::cout << "Read from CMOS addr 0x" << std::hex << addr << '\n';
    switch(addr) {
    default:
      std::cout << "Unsupported CMOS address for read.\n";
      //exit(0);
    }
  }

  void io_cb(int c, uint64_t p, uint8_t s, int w, uint32_t v) {
    tracefile << std::dec << c << ": I/O " << (w?"WR":"RD") << ": (0x" 
              << std::hex << p << "): " << std::dec << (unsigned)(s*8) 
              << " bits";

    if (w) tracefile << ": 0x" << std::hex << v;
    tracefile << '\n'; 

    if (w) {
      switch(p) {
      case 0x20: // PIC control register
      case 0x21: // Master PIC mask.
        break;
      case 0x40: // PIT Divisor/counter 0 set
      case 0x41: // PIT counter 1
      case 0x42: // PIT counter 2
      case 0x43: // PIT command
        break;
      case 0x61: // Keyboard control port
        break;
      case 0x70:
        std::cout << "CMOS address: 0x" << std::hex << v << '\n';
        cmos_addr = v;
        break;
      case 0x71:
        cmos_wr(cmos_addr, v);
      case 0x80: // Written by Linux kernel as a delay/barrier.
        break;
      case 0xa0: // Slave PIC control register
      case 0xa1: // Slave PIC mask.
        break;
      case 0xf0: // 80x87 control.
      case 0xf1:
        break;
      case 0x3f8: //
      case 0x3f9: ////
      case 0x3fa: //////
      case 0x3fb: ////////  Serial port control
      case 0x3fc: ////////      registers
      case 0x3fd: //////
      case 0x3fe: ////
      case 0x3ff: //
        break;
      case 0x4d0: // EISA interrupt edge/level select mask.
      case 0x4d1:
        break;
      default:
        std::cout << "Unsupported port address for write.\n";
        //exit(0);
      }
    } else {
      switch(p) {
      case 0x21: // PIC mask.
        break;
      case 0x40: // PIT counter
      case 0x41:
      case 0x42:
        break;
      case 0x60: // Keyboard data buffer
      case 0x61: // Keyboard control port
        break;
      case 0x64: // Keyboard controller status
        break;
      case 0x71:
        cmos_rd(cmos_addr);
      case 0x3f8: //
      case 0x3f9: ////
      case 0x3fa: //////
      case 0x3fb: ////////  Serial port control
      case 0x3fc: ////////      registers
      case 0x3fd: //////
      case 0x3fe: ////
      case 0x3ff: //
        break;
      case 0x4d0: // EISA interrupt edge/level select mask.
      case 0x4d1:
        break;
      default:
        std::cout << "Unsupported port address for read.\n";
        //exit(0);
      }
    }
  }

  void reg_cb(int c, int r, uint8_t s, int type) {
    tracefile << std::dec << c << (s == 0?": Flag ":": Reg ") 
              << (type?"WR":"RD") << std::dec;

    if (s != 0) tracefile << ' ' << r << ": " << (unsigned)(s*8) << " bits.\n";
    else tracefile << ": mask=0x" << std::hex << r << '\n';
  }

private:
  OSDomain &osd;
  ostream &tracefile;
  bool finished;
  uint8_t cmos_addr;

  static const char * itype_str[];
};

const char *TraceWriter::itype_str[] = {
  "QSIM_INST_NULL",
  "QSIM_INST_INTBASIC",
  "QSIM_INST_INTMUL",
  "QSIM_INST_INTDIV",
  "QSIM_INST_STACK",
  "QSIM_INST_BR",
  "QSIM_INST_CALL",
  "QSIM_INST_RET",
  "QSIM_INST_TRAP",
  "QSIM_INST_FPBASIC",
  "QSIM_INST_FPMUL",
  "QSIM_INST_FPDIV"
};

int main(int argc, char** argv) {
  using std::istringstream;
  using std::ofstream;

  ofstream *outfile(NULL);

  unsigned n_cpus = 2;

  // Read in the kernel symtable
  read_sys_map("../linux64/linux-2.6.34/System.map");

  // Read number of CPUs as a parameter. 
  if (argc >= 2) {
    istringstream s(argv[1]);
    s >> n_cpus;
  }

  // Read trace file as a parameter.
  if (argc >= 3) {
    outfile = new ofstream(argv[2]);
  }

  OSDomain *osd_p(NULL);
  OSDomain &osd(*osd_p);

  if (argc >= 4) {
    // Create new OSDomain from saved state.
    osd_p = new OSDomain(argv[3]);
    n_cpus = osd.get_n();
  } else {
    osd_p = new OSDomain(n_cpus, "../linux64/bzImage");
  }

  mk_sfi_table(*osd_p);

  // Attach a TraceWriter if a trace file is given.
  TraceWriter tw(osd, outfile?*outfile:std::cout);

  // If this OSDomain was created from a saved state, the app start callback was
  // received prior to the state being saved.
  if (argc >= 4) tw.app_start_cb(0);

  osd.connect_console(std::cout);

  // The main loop: run until 'finished' is true.
  while (!tw.hasFinished()) {
    for (unsigned i = 0; i < 100; i++) {
      for (unsigned j = 0; j < n_cpus; j++) {
           osd.run(j, 10000);
      }
    }
    osd.timer_interrupt();
  }
  
  if (outfile) { outfile->close(); }
  delete outfile;
  delete osd_p;

  return 0;
}

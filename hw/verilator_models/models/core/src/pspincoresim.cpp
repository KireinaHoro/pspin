// Copyright 2020 ETH Zurich
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "pspincoresim.hpp"

using namespace PsPIN;

#include <elf.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "AXIPort.hpp"
#include "pspin.hpp"

PsPINCoreSim::PsPINCoreSim(std::string vcd_file_path) 
{
    Verilated::commandArgs(0, (char**) NULL);
    Vpspin_verilator *tb = new Vpspin_verilator();
    sim = new SimControl<Vpspin_verilator>(tb, vcd_file_path.c_str());

    // Define ports
    AXI_MASTER_PORT_ASSIGN(tb, ni_slave, &ni_data_mst_port);
    AXI_MASTER_PORT_ASSIGN(tb, no_slave, &no_data_mst_port);
    NI_CTRL_PORT_ASSIGN(tb, her, &ni_ctrl_mst_port)    
    NO_CMD_PORT_ASSIGN(tb, nic_cmd, &no_ctrl_slv_port);
    AXI_SLAVE_PORT_ASSIGN(tb, host_master, &host_slv_port);
    AXI_MASTER_PORT_ASSIGN(tb, host_slave, &host_mst_port);

    host_data_slv = new DMATarget<AXIPort<uint64_t, uint64_t>>(host_slv_port, 0, 0);
    host_data_mst = new DMAEngine<AXIPort<uint32_t, uint64_t>>(host_mst_port);
    
    ni_data_mst = new DMAEngine<AXIPort<uint32_t, uint64_t>>(ni_data_mst_port);
    no_data_mst = new DMAEngine<AXIPort<uint32_t, uint64_t>>(no_data_mst_port);

    ni_ctrl_mst = new NIMaster(ni_ctrl_mst_port);    
    no_ctrl_slv = new NOSlave(no_ctrl_slv_port);

    sim->add_module(*host_data_slv);
    sim->add_module(*host_data_mst);
    sim->add_module(*ni_data_mst);
    sim->add_module(*no_data_mst);
    sim->add_module(*ni_ctrl_mst);
    sim->add_module(*no_ctrl_slv);

    // Send reset signal
    sim->reset();

    // wait for active signal
    uint8_t done_flag;
    while (!(*(ni_ctrl_mst_port.pspin_active_i))) {
        step(&done_flag);
        assert(!done_flag);
    }
}

int PsPINCoreSim::run() 
{
    // Main loop
    sim->run_all();
    return SPIN_SUCCESS;
}

int PsPINCoreSim::step(uint8_t* done_flag)
{
    // Single step of the main loop
    *done_flag = 0;
    sim->run_single();
    if (sim->done()) *done_flag = 1;

    return SPIN_SUCCESS;
}

int PsPINCoreSim::shutdown()
{
    ni_ctrl_mst->shutdown();
    return SPIN_SUCCESS;
}

void PsPINCoreSim::printStats()
{
    printf("\n###### Statistics ######\n");
    for (auto it = sim->get_modules().begin(); it != sim->get_modules().end(); ++it){
        it->get().print_stats();
        printf("----------------------------------\n");
    }
}

int PsPINCoreSim::nicMemWrite(uint32_t addr, uint8_t *data, size_t size, WriteCompletedCallback cb)
{
    ni_data_mst->write(addr, data, size, cb);
    return SPIN_SUCCESS;
}

int PsPINCoreSim::nicMemRead(uint32_t addr, uint8_t *data, size_t size, ReadCompletedCallback cb)
{
    no_data_mst->read(addr, data, size, cb);
    return SPIN_SUCCESS;
}

int PsPINCoreSim::hostMemWrite(uint32_t addr, uint8_t *data, size_t size, WriteCompletedCallback cb)
{
    host_data_mst->write(addr, data, size, cb);
    return SPIN_SUCCESS;
}

int PsPINCoreSim::hostMemRead(uint32_t addr, uint8_t *data, size_t size, ReadCompletedCallback cb)
{
    host_data_mst->read(addr, data, size, cb);
    return SPIN_SUCCESS;
}

int PsPINCoreSim::sendHER(her_descr_t *her, HERCompletetionCallback cb)
{
    ni_ctrl_mst->send_her(her, cb);
    return SPIN_SUCCESS;
}

int PsPINCoreSim::setNICCommandCallback(NICCommandCallback cb) { 
    no_ctrl_slv->set_nic_command_cb(cb);
    return SPIN_SUCCESS;
}

int PsPINCoreSim::setHostWriteCallback(HostWriteCallback cb)
{
    host_data_slv->set_slv_write_cb(cb);
    return SPIN_SUCCESS;
}

int PsPINCoreSim::setHostReadCallback(HostReadCallback cb)
{
    host_data_slv->set_slv_read_cb(cb);
    return SPIN_SUCCESS;
}

bool PsPINCoreSim::setNICCommandStatus(bool blocked)
{
    bool status = no_ctrl_slv->is_blocked();
    if (blocked) no_ctrl_slv->block();
    else no_ctrl_slv->unblock();

    return status;
}

bool PsPINCoreSim::setHostWriteStatus(bool blocked)
{
    bool status = host_data_slv->is_write_blocked();
    if (blocked) host_data_slv->block_writes();
    else host_data_slv->unblock_writes();
    
    return status;
}

bool PsPINCoreSim::setHostReadStatus(bool blocked)
{
    bool status = host_data_slv->is_read_blocked();
    if (blocked) host_data_slv->block_reads();
    else host_data_slv->unblock_reads();
    
    return status;
}

int PsPINCoreSim::findHandlerByName(const char *binfile, const char* handler_name, uint32_t *handler_addr, size_t *handler_size)
{
    struct stat sb;
    FILE *f = fopen(binfile, "rb");
    int fno = fileno(f);
    fstat(fno, &sb);

    Elf32_Ehdr *elf_ptr = (Elf32_Ehdr*) mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fno, 0);
    uint32_t num_sections = elf_ptr->e_shnum;

    Elf32_Shdr *section_header_table = (Elf32_Shdr *)(((uint8_t *)elf_ptr) + elf_ptr->e_shoff);

    Elf32_Shdr *section_header_string_table = &(section_header_table[elf_ptr->e_shstrndx]);
    char *sec_string_table = (char *)(((uint8_t *)elf_ptr) + section_header_string_table->sh_offset);

    //find string table for the symbols
    char *sym_string_tab = NULL;
    for (int i = 0; i < num_sections; i++)
    {
        //printf("section: %s\n", sec_string_table + section_header_table[i].sh_name);
        char *sec_name = sec_string_table + section_header_table[i].sh_name;
        if (!strcmp(sec_name, ".strtab"))
        {
            sym_string_tab = (char *)(((uint8_t *)elf_ptr) + section_header_table[i].sh_offset);
        }
    }
    assert(sym_string_tab != NULL);

    for (int i = 0; i < num_sections; i++)
    {
        if (section_header_table[i].sh_type == SHT_SYMTAB)
        {
            //printf("sh type: %u; sh addr: %x\n", section_header_table[i].sh_type, section_header_table[i].sh_addr);

            Elf32_Sym *symbol_table = (Elf32_Sym *)(((uint8_t *)elf_ptr) + section_header_table[i].sh_offset);
            uint32_t num_symbols = section_header_table[i].sh_size / sizeof(Elf32_Sym);

            for (int j = 0; j < num_symbols; j++)
            {
                char *sym_name = sym_string_tab + symbol_table[j].st_name;

                if (!strcmp(sym_name, handler_name))
                {
                    *handler_addr = symbol_table[j].st_value;
                    *handler_size = 4096;
                    break;
                }
            }
        }
    }

    munmap(elf_ptr, sb.st_size);
    fclose(f);

    return SPIN_SUCCESS;
}

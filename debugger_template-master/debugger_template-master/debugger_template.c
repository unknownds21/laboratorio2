#include <stdio.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/personality.h>
#include "linenoise.h"

struct breakpoint{
    uint64_t addr;
    uint8_t prev_opcode;
    uint8_t active;
};

struct debugee{
    char* name;
    pid_t pid;
};

struct reg_descriptor {
    int dwarf_r;
    char* name;
};

const int n_registers = 27;

const struct reg_descriptor g_register_descriptors[] = {
    { 0, "r15" },
    { 1, "r14" },
    { 2, "r13" },
    { 3, "r12" },
    { 4, "rbp" },
    { 5, "rbx" },
    { 6, "r11" },
    { 7, "r10" },
    { 8, "r9" },
    { 9, "r8" },
    { 10, "rax" },
    { 11, "rcx" },
    { 12, "rdx" },
    { 13, "rsi" },
    { 14, "rdi" },
    { 15, "orig_rax" },
    { 16, "rip" },
    { 17, "cs" },
    { 18, "eflags" },
    { 19, "rsp" },
    { 20, "ss" },
    { 21, "fs_base" },
    { 22, "gs_base" },
    { 23, "ds" },
    { 24, "es" },
    { 25, "fs" },
    { 26, "gs" },
};

void handle_command(char*);

void wait_for_signal(int*);
int is_command(char*, char*);
void continue_execution();
void enable_breakpoint(struct breakpoint*);
void disable_breakpoint(struct breakpoint*);
uint64_t* get_register_address(char*, struct user_regs_struct*);
uint64_t read_memory(uint64_t);
void write_memory(uint64_t, uint64_t);
const struct reg_descriptor* get_register_by_name(char*);
const struct reg_descriptor* get_register_by_dwarf_number(int);

void step_over_breakpoint();
uint64_t get_pc();
void set_pc(uint64_t);


struct debugee *child;
struct breakpoint *breakpt;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Program name not specified");
        return -1;
    }
    child = (struct debugee*)malloc(sizeof(struct debugee));
    breakpt  = (struct breakpoint*)malloc(sizeof(struct breakpoint));
    breakpt->active = 0;

    child->name = argv[1];
    child->pid = fork();

    if (child->pid == 0) {
        personality(ADDR_NO_RANDOMIZE);
        ptrace(PTRACE_TRACEME, 0, NULL, NULL);
        execl(child->name, child->name, NULL);
    }else if (child->pid >= 1)  {
        int status;
        int options = 0;
        waitpid(child->pid, &status, options);
        char* line = NULL;
        while((line = linenoise("minidbg> ")) != NULL) {
            handle_command(line);
            linenoiseHistoryAdd(line);
            linenoiseFree(line);
        }
    }

    free(child);
    free(breakpt);
    return 0;
}

void handle_command(char* line) {
    //At this point you must to implement all the logic to manage the inputs of the program:
    //continue -> To continue the execution of the program
    //next -> To go step by step
    //register write/read <reg_name> <value>(when write format 0xVALUE) -> To read/write the value of a register (see the global variable g_register_descriptors)
    //break <0xVALUE> (Hexadecimal) -> To put a breakpoint in an adress

    char* inputs[5];
    int counter_inputs = 0;
    char* copy_of_line;
    copy_of_line = strdup(line);
    char * token = strtok(copy_of_line, " ");

    while( token != NULL ) {
        inputs[counter_inputs] = strdup(token);
        token = strtok(NULL, " ");
        counter_inputs++;
        if(counter_inputs == 5) printf("You can provide up to 10 words in the input");
    } 

    char* command = inputs[0];

    if(is_command(command, "continue")){
        continue_execution();
    }else if(is_command(command, "break")){
        breakpt->addr = (uint64_t)strtol(inputs[1], NULL, 0);
        enable_breakpoint(breakpt);
    }else if (is_command(command, "register")){
        struct user_regs_struct regs;
        uint64_t *register_address;
        ptrace(PTRACE_GETREGS, child->pid, NULL, &regs);
        register_address = get_register_address(inputs[2], &regs);
        if(is_command(inputs[1], "read")){
            printf("Value of register: %ld", *register_address);
        }
        else if (is_command(inputs[1], "write")){
            *register_address = (uint64_t)strtol(inputs[3], NULL, 0);
            ptrace(PTRACE_SETREGS, child->pid, NULL, &regs);
        }
    }else if (is_command(command, "memory")) {
        if(is_command(inputs[1], "read")){
            printf("Value in memory: %ld", read_memory((uint64_t)strtol(inputs[2], NULL, 0)));
        }
        else if (is_command(inputs[1], "write")){
            write_memory((uint64_t)strtol(inputs[2], NULL, 0), (uint64_t)strtol(inputs[3], NULL, 0));
        }
    }
    else{
        printf("Unknown command\n");
    }

    //The following lines show a basic example of how to use the PTRACE API

    //Read the registers
    //struct user_regs_struct regs;
    //uint64_t *register_address;
    //ptrace(PTRACE_GETREGS, child->pid, NULL, &regs);

    //Write the registers -> If you want to change a register, you must to read them first using the previous call, modify the struct user_regs_struct
    //(the register that you want to change) and then use the following call
    //ptrace(PTRACE_SETREGS, child->pid, NULL, &regs);

    //If you want to enable a breakpoint (in a provided adress, for example 0x555555554655), you must to use the following CALL
    //breakpt->addr =  ((uint64_t)strtol("0x555555554655", NULL, 0));
    //uint64_t data = ptrace(PTRACE_PEEKDATA, child->pid, breakpt->addr, NULL);
    //breakpt->prev_opcode = (uint8_t)(data & 0xff);
    //uint64_t int3 = 0xcc;
    //uint64_t data_with_int3 = ((data & ~0xff) | int3);
    //ptrace(PTRACE_POKEDATA, child->pid, breakpt->addr, data_with_int3);
    //breakpt->active = 1;

    //To disable a breakpoint
    //data = ptrace(PTRACE_PEEKDATA, child->pid, breakpt->addr, NULL);
    //uint64_t restored_data = ((data & ~0xff) | breakpt->prev_opcode);
    //ptrace(PTRACE_POKEDATA, child->pid, breakpt->addr, restored_data);
    //breakpt->active = 0;

    //To execute a singe step
    //ptrace(PTRACE_SINGLESTEP, child->pid, NULL, NULL);

    //To read the value in a memory adress
    //uint64_t value_in_memory = (uint64_t)ptrace(PTRACE_PEEKDATA, child->pid, (uint64_t)strtol("0x555555554655", NULL, 0), NULL);

    //To write a value in an adress
    //ptrace(PTRACE_POKEDATA, child->pid, (uint64_t)strtol("0x555555554655", NULL, 0), (uint64_t)strtol("0x555555554655", NULL, 0));

    //If you want to continue with the execution of the debugee program
    //ptrace(PTRACE_CONT, child->pid, NULL, NULL);
    //int status;
    //int options = 0;
    //waitpid(child->pid, &status, options);
}

int is_command(char* input, char* reference){
    int starts_with = 1;
    for (int i = 0; reference[i] != '\0'; i++) {
        if (input[i] != reference[i]) {
            starts_with = 0;
            break;
        }
    }
    return starts_with;
}

void continue_execution(){
    step_over_breakpoint();
    ptrace(PTRACE_CONT, child->pid, NULL, NULL);
    int status;
    wait_for_signal(&status);
}

void wait_for_signal(int* wait_status){
    int options = 0;
    waitpid(child->pid, wait_status, options);
}

void enable_breakpoint(struct breakpoint* bp){
    uint64_t data = ptrace(PTRACE_PEEKDATA, child->pid, bp->addr, NULL);
    bp->prev_opcode = (uint8_t)(data & 0xff);
    uint64_t int3 = 0xcc;
    uint64_t data_with_int3 = ((data & ~0xff) | int3);
    ptrace(PTRACE_POKEDATA, child->pid, bp->addr, data_with_int3);
    bp->active = 1;
}

void disable_breakpoint(struct breakpoint* bp) {
    uint64_t data = ptrace(PTRACE_PEEKDATA, child->pid, bp->addr, NULL);
    uint64_t restored_data = ((data & ~0xff) | bp->prev_opcode);
    ptrace(PTRACE_POKEDATA, child->pid, bp->addr, restored_data);
    bp->active = 0;
}

const struct reg_descriptor* get_register_by_name(char* input_name){
    for(int i=0; i<n_registers; i++){
        if(strcmp(g_register_descriptors[i].name, input_name) == 0){
            return (const struct reg_descriptor*)&g_register_descriptors[i];
        }
    }
    return NULL;
}

const struct reg_descriptor* get_register_by_dwarf_number(int number){
    for(int i=0; i<n_registers; i++){
        if(g_register_descriptors[i].dwarf_r == number){
            return (const struct reg_descriptor*)&g_register_descriptors[i];
        }
    }
    return NULL;
}

uint64_t* get_register_address(char* input, struct user_regs_struct* regs){
    uint64_t *ptr;
    const struct reg_descriptor *reg_desc;
    reg_desc = get_register_by_name(input);
    switch(reg_desc->dwarf_r){
        case 0:
            ptr = (uint64_t*)&(regs->r15);
            break;
        case 1:
            ptr = (uint64_t*)&(regs->r14);
            break;
        case 2:
            ptr = (uint64_t*)&(regs->r13);
            break;
        case 3:
            ptr = (uint64_t*)&(regs->r12);
            break;
        case 4:
            ptr = (uint64_t*)&(regs->rbp);
            break;
        case 5:
            ptr = (uint64_t*)&(regs->rbx);
            break;
        case 6:
            ptr = (uint64_t*)&(regs->r11);
            break;
        case 7:
            ptr = (uint64_t*)&(regs->r10);
            break;
        case 8:
            ptr = (uint64_t*)&(regs->r9);
            break;
        case 9:
            ptr = (uint64_t*)&(regs->r8);
            break;
        case 10:
            ptr = (uint64_t*)&(regs->rax);
            break;
        case 11:
            ptr = (uint64_t*)&(regs->rcx);
            break;
        case 12:
            ptr = (uint64_t*)&(regs->rdx);
            break;
        case 13:
            ptr = (uint64_t*)&(regs->rsi);
            break;
        case 14:
            ptr = (uint64_t*)&(regs->rdi);
            break;
        case 15:
            ptr = (uint64_t*)&(regs->orig_rax);
            break;
        case 16:
            ptr = (uint64_t*)&(regs->rip);
            break;
        case 17:
            ptr = (uint64_t*)&(regs->cs);
            break;
        case 18:
            ptr = (uint64_t*)&(regs->eflags);
            break;
        case 19:
            ptr = (uint64_t*)&(regs->rsp);
            break;
        case 20:
            ptr = (uint64_t*)&(regs->ss);
            break;
        case 21:
            ptr = (uint64_t*)&(regs->fs_base);
            break;
        case 22:
            ptr = (uint64_t*)&(regs->gs_base);
            break;
        case 23:
            ptr = (uint64_t*)&(regs->ds);
            break;
        case 24:
            ptr = (uint64_t*)&(regs->es);
            break;
        case 25:
            ptr = (uint64_t*)&(regs->fs);
            break;
        case 26:
            ptr = (uint64_t*)&(regs->gs);
            break;
        default:
            ptr = (uint64_t*)NULL;
    }
    return ptr;
}

uint64_t read_memory(uint64_t address) {
    uint64_t value_in_memory = (uint64_t)ptrace(PTRACE_PEEKDATA, child->pid, address, NULL)
    return value_in_memory;
}

void write_memory(uint64_t address, uint64_t new_value) {
    ptrace(PTRACE_POKEDATA, child->pid, address, new_value);
}

void step_over_breakpoint() {
    uint64_t possible_breakpoint_location = get_pc() - 1;
    if(breakpt->active ==1){
        if(possible_breakpoint_location == breakpt->addr){
            uint64_t previous_instruction_address = possible_breakpoint_location;
            set_pc(previous_instruction_address);
            disable_breakpoint(breakpt);
            ptrace(PTRACE_SINGLESTEP, child->pid, NULL, NULL);
            int status;
            wait_for_signal(&status);
            enable_breakpoint(breakpt);
        }
    }
}

uint64_t get_pc() {
    struct user_regs_struct regs;
    ptrace(PTRACE_GETREGS, child->pid, NULL, &regs);
    return (uint64_t)regs.rip;
}

void set_pc(uint64_t pc) {
    struct user_regs_struct regs;
    ptrace(PTRACE_GETREGS, child->pid, NULL, &regs);
    regs.rip = pc;
    ptrace(PTRACE_SETREGS, child->pid, NULL, &regs);
}

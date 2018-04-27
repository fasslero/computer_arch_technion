#include "sim_api.h"

void init_clk_tick(void);
void print_state();


// pipeline Stages
void do_pipe_fetch(void);
void do_pipe_decode(void);
void do_pipe_execute(void);
void do_pipe_mem(void);
// Returns 0 on success, -1 on wait-state
int do_pipe_mem_load(void);
void do_pipe_wb(void);
void do_write_to_reg_file(void);
// Take Brunch - Flush and load from address
void flush_buffer(int number);
void do_branch_if_needed(void);
void do_flush_to_i_stages(int i);

void do_halt_if_needed(void); //todo

// Data-Hazard Detection Unit. Also updates in case of hazard.
void do_hdu(void); //does data hdu for all stages and pc-4 if needed
void data_hdu_i_to_decode(void);
int load_hdu_mem_id(void); // todo
void data_hdu_wb_if(void); //todo?

// The simulator pipeline
SIM_coreState pipeline;

//simulator flags
bool forwarding;
bool split_regfile;

// The simulator data
int32_t buffer_dst_reg_data[SIM_PIPELINE_DEPTH];
int32_t buffer_result_data[SIM_PIPELINE_DEPTH];
int32_t buffer_pc[SIM_PIPELINE_DEPTH];

int tick_number = 0;
int branch_address = 0;
bool halt_flag = 0;


/*! SIM_CoreReset: Reset the processor core simulator machine to start new simulation
  Use this API to initialize the processor core simulator's data structures.
  The simulator machine must complete this call with these requirements met:
  - PC = 0  (entry point for a program is at address 0)
  - All the register file is cleared (all registers hold 0)
  - The value of IF is the instuction in address 0x0
  \returns 0 on success. <0 in case of initialization failure.
*/
int SIM_CoreReset(void) {
    pipeline.pc = 0; //todo - zero or -4?

    //init the reg file
    for (int i = 0; i < SIM_REGFILE_SIZE; ++i) {
        pipeline.regFile[i] = 0;
    }

    for (int j = 0; j < SIM_PIPELINE_DEPTH ; ++j) {
        //reset the command
        reset_cmd(&pipeline.pipeStageState[j].cmd);
        //reset the current state values
        pipeline.pipeStageState[j].src1Val = 0;
        pipeline.pipeStageState[j].src2Val = 0;
    }
    return 0;
}
/**
 * init the cmd struct
 *
 */
void reset_cmd(PipeStageState* PipeStageState){
    PipeStageState->cmd.dst = 0;
    PipeStageState->cmd.isSrc2Imm = false;
    PipeStageState->cmd.opcode = 0;
    PipeStageState->cmd.src1 = 0;
    PipeStageState->cmd.src2 = 0;
}

/*! SIM_CoreClkTick: Update the core simulator's state given one clock cycle.
  This function is expected to update the core pipeline given a clock cycle event.
*/
void SIM_CoreClkTick() {
    //clk tick (pc+4 and so on)
    ++tick_number;

    do_pipe_WB();

    if (do_pipe_mem() != 0)
        return;

    do_hdu();

    do_pipe_execute();

    do_pipe_decode();

    do_pipe_fetch();
}

/*! SIM_CoreGetState: Return the current core (pipeline) internal state
    curState: The returned current pipeline state
    The function will return the state of the pipe at the end of a cycle
*/
void SIM_CoreGetState(SIM_coreState *curState) {
    curState = *pipeline;
}


/**********************************
 *
 * pipe stages commands
 *
 **********************************/

// get a new command from the pc address and store it in the first pipe line stage
void do_pipe_fetch(void){
    SIM_cmd* next_command = (SIM_cmd*)malloc(sizeof(SIM_cmd));
    //will change the pipeline pc and flush the commands if needed.
    do_branch_if_needed();
    SIM_MemInstRead(pipeline.pc, nextCommand);

    //if not split reg file, write now the data to the reg file
    if (!split_regfile){
        do_write_to_reg_file();
    }
    //update next pipeBuffer data
    pipeline.pipeStageState[FETCH].cmd = *nextCommand;

    //advance the pc to the next command
    pipeline.pc += 4;
    //save the pc+4 to the branch instructions
    buffer_pc[0] = pipeline.pc;
    free(nextCommand);

    do_halt_if_needed();
}

//decode the command and store the data in the SIM_cmd struct
void do_pipe_decode(void){
    PipeStageState* decode_pipe_stage = &pipeline.pipeStageState[FETCH];
    SIM_cmd* command = &decode_pipe_stage.cmd;

    //the first value is always red from the memory
    decode_pipe_stage->src1Val = pipeline.regFile[command->src1];
    //keep the dst register
    buffer_dst_reg_data[1] = pipeline.regFile[command->dst];
    
    //in immediate command the second src value is taken from the command
    if (command->isSrc2Imm) {
        decode_pipe_stage->src2Val = command->src2;
    }
    else
    {
        decode_pipe_stage->src2Val = pipeline.regFile[command->src2];
    }
    //advance the pipe
    pipeline.pipeStageState[DECODE] = pipeline.pipeStageState[FETCH];
    buffer_pc[DECODE] = buffer_pc[FETCH]; //take the pc with you
}

//execute the command
void do_pipe_execute(void){

    // Get the command
    PipeStageState* execute_pipe_state = &pipeline.pipeStageState[DECODE];
    SIM_cmd* command = &execute_pipe_state.cmd;

    switch (command->opcode){

        // R-Type Commands
        // Do ALU commands
        // I'v added the addi and subi command here, is it ok? the number doesn't need to be signed extended?
        case CMD_ADD:
        case CMD_ADDI:
            buffer_result_data[EXECUTE] = execute_pipe_state->src1Val + execute_pipe_state->src2Val;
            break;

        case CMD_SUB:
        case CMD_SUBI:
            buffer_result_data[EXECUTE] = execute_pipe_state->src1Val - execute_pipe_state->src2Val;
            break;

        // I-Type Commands
            // Calculate memory address
        case CMD_LOAD:
            // calc the address by adding the immediate value. buffer_result_data now has the address to load from.
            buffer_result_data[EXECUTE] = execute_pipe_state->src1Val + execute_pipe_state->src2Val;
            break;
        case CMD_STORE:
            buffer_result_data[EXECUTE] = command->dst + execute_pipe_state->src2Val;

        case CMD_BR:
        case CMD_BREQ:
        case CMD_BRNEQ:
            // pc + 4 + dest
            buffer_result_data[EXECUTE] = command->dst + buffer_pc[DECODE];
        case CMD_HALT: //todo halt logic

        default:
            buffer_result_data[EXECUTE] = 0;
        break;
    }
    
    pipeline.pipeStageState[EXECUTE] = pipeline.pipeStageState[DECODE];
    buffer_result_data[EXECUTE]= buffer_result_data[DECODE];
    buffer_dst_reg_data[EXECUTE] = buffer_dst_reg_data[DECODE];
}

//store, load or branch commands
void do_pipe_mem(void){
    // Get the command
    SIM_cmd* command = &pipeline.pipeStageState[EXECUTE].cmd;
    PipeStageState* mem_pipe_state = &pipeline.pipeStageState[EXECUTE];

    int32_t memory_address = buffer_result_data[EXECUTE]; //was calculated in the last cycle
    int32_t calculated_branch_address = buffer_result_data[EXECUTE];

    switch (command->opcode){
        case CMD_BR:
            branch_address = calculated_branch_address;
            break;
        case CMD_BREQ:
            if (mem_pipe_state->src1Val == mem_pipe_state->src2Val){
                branch_address = calculated_branch_address;
            }
            break;
        case CMD_BRNEQ:
            if (mem_pipe_state->src1Val != mem_pipe_state->src2Val){
                branch_address = calculated_branch_address;
            }
            break;
        case CMD_LOAD:
            //may take more then one cycle, if so all the pipe will wait
            return do_pipe_mem_load();
        case CMD_STORE:
            //will end in one cycle
            SIM_MemDataWrite(memory_address, mem_pipe_state->src1Val);
            break;

        default:
            break;
    }
    pipeline.pipeStageState[MEMORY] = pipeline.pipeStageState[EXECUTE];
    buffer_result_data[MEMORY]= buffer_result_data[EXECUTE];
    buffer_dst_reg_data[MEMORY] = buffer_dst_reg_data[EXECUTE];

    return 0;
}


//do the mem load, may take more then 1 clk. if so, halt the pipe line until the load is finished
int do_pipe_mem_load(void){
    SIM_cmd* command = &pipeline.pipeStageState[EXECUTE].cmd;
    PipeStageState* mem_pipe_state = &pipeline.pipeStageState[EXECUTE];

    int32_t memory_address = buffer_result_data[EXECUTE]; //was calculated in the last cycle

    // Allocate the data space
    int32_t* data = (int32_t*)malloc(sizeof(int32_t));

    //try to load the data
    //if the data wasn't retrieved return -1
    if (SIM_MemDataRead(memoryAddress, data) == -1)
    {

        // Undo pc update
        pipeline.pc -= 4;
        // The WB operation is done, thus flush the pipe there
        // we flush buffer 3, this is the one that was handled in the WB stage!
        flush_buffer(MEMORY);
        return -1;
    }
    else{
        //save the data
        buffer_result_data[MEMORY] = *data;
        free(data);

        return 0;
    }
}


void do_pipe_wb(void){
    SIM_cmd* command = &pipeline.pipeStageState[MEMORY].cmd;
    PipeStageState* wb_pipe_state = &pipeline.pipeStageState[MEMORY];
    int dst_register = command->dst;

    //if need to WB, write to the buffer.
    switch (wb_pipe_state->cmd){
        case CMD_SUB:
        case CMD_ADD:
        case CMD_SUBI:
        case CMD_ADDI:
        case CMD_LOAD:
            buffer_result_data[MEMORY] = buffer_result_data[MEMORY];
        default:
            //we don't want to write data, so we write 0 to the 0 register
            buffer_result_data[MEMORY] = 0;
            command->dst = 0; 
    }

    //use another buffer for the WB if split reg file is not active
    pipeline.pipeStageState[WRITEBACK] = pipeline.pipeStageState[MEMORY];
    buffer_dst_reg_data[WRITEBACK] = buffer_dst_reg_data[MEMORY];

    // if split reg file is on write to the reg file, else this will be done in the beginning of the fetch cycle
    if (split_regfile){
        do_write_to_reg_file();
    }
}

/*!
 * write to the reg file
 */
void do_write_to_reg_file(void){
    SIM_cmd* command = &pipeline.pipeStageState[WRITEBACK].cmd;
    PipeStageState* wb_pipe_state = &pipeline.pipeStageState[WRITEBACK];
    int dst_register = command->dst;

    pipeline.regFile[dst_register] = buffer_result_data[WRITEBACK];
}


/*!
 * flush a pipe stage includes the buffered data and the pc
 */

void flush_buffer(int stage){
    PipeStageState* pipe_stage_to_flush = &pipeline.pipeStageState[stage];

    pipe_stage_to_flush->src2Val = 0;
    pipe_stage_to_flush->src1Val = 0;
    reset_cmd(pipe_stage_to_flush);
    buffer_result_data[stage] = 0;
    buffer_dst_reg_data[stage] = 0;
    buffer_pc[stage] = 0;
}

/*!
 * flush a number of stages, use if branch was taken
 */
void do_flush_to_i_stages(int i){

    //flush stages 0 to i in the pipe
    for (int j = 0; j < i; ++j) {
        flush_buffer(j);
    }

}

/*!
 * do branch - set pipeline pc the new branch address,
 * flush the 3 first stages and reset the global variable branch_address
 */

void do_branch_if_needed(void){
    if (branch_address != 0){
        pipeline.pc = branch_address;
        do_flush_to_i_stages(3);
        branch_address = 0;
    }
}

/*!
 * halt the command - if CMD_HALT flush the pipe stage and sub 4 from pc
 */
void do_halt_if_needed(void){
    PipeStageState* pipe_stage_to_halt = &pipeline.pipeStageState[FETCH];
    SIM_cmd* command = &pipeline.pipeStageState[FETCH].cmd;

    if (halt_flag == 1){
        flush_buffer(FETCH);
    }
    else if(command->opcode == CMD_HALT && halt_flag == 0){
        halt_flag = 1;
    }
    pipeline.pc -= 4;

}
/*
 * print function for debug
 */

void print_state()
{
    printf("\nSimulation on cycle %d. The state is:\n", tick_number);
    DumpCoreState(&pipeline);
    printf("\n");
}
/*******************************
 * HDUs
 *******************************/
void do_hdu(void){

    if (data_hdu_i_to_decode(WRITEBACK) == -1 ||
        data_hdu_i_to_decode(MEMORY)    == -1 ||
        data_hdu_i_to_decode(EXECUTE)   == -1 ||
        load_hdu_mem_id() == -1)
    {
        pipeline.pc -= 4;
    }
}

/*!
 * Check whether the src and dst registers on pipe i and ID are the same.
   If forwarding is active: copy the register values from the i stage to the ID stage.
   If forwarding is disabled: enter a bubble (NOP command) in the EXE pipe state
 */

void data_hdu_i_to_decode(int STAGE) {
// return -1 if there was a data hazard and execute stage was flushed

    // Get the commands
    SIM_cmd *stage_cmd = &pipeline.pipeStageState[STAGE].cmd;
    SIM_cmd *decode_cmd = &pipeline.pipeStageState[DECODE].cmd; // The values in the entrance of the execute stage

    // Work only if the command in the EXE buffer is write command
    if (stage_cmd->opcode != CMD_ADD && stage_cmd->opcode != CMD_SUB)
        return;
    if (stage_cmd->opcode != CMD_ADDI && stage_cmd->opcode != CMD_SUBI)
        return;

    //flags to active the HDU
    bool dst_and_src1_are_equal = stage_cmd->dst == decode_cmd->src1;
    bool dst_and_dst_are_equal = stage_cmd->dst == decode_cmd->dst;
    bool dst_and_src2_are_equal_not_imm = !stage_cmd->isSrc2Imm &&
            !decode_cmd->isSrc2Imm && stage_cmd->dst == decode_cmd->src2;

    if(forwarding) {

        // Check the value of register src1
        if (dst_and_src1_are_equal)
            pipeline.pipeStageState[DECODE].src1Val = buffer_result_data[STAGE];

        // Check the value of register dst
        if (dst_and_dst_are_equal)
            buffer_dst_reg_data[DECODE] = buffer_result_data[STAGE];

        // Check the value of register src2, only if it is not immediate
        if (dst_and_src2_are_equal_not_imm)
                pipeline.pipeStageState[DECODE].src2Val = buffer_result_data[STAGE];
        return 0;
    }
    else {
        if (dst_and_dst_are_equal || dst_and_src1_are_equal || dst_and_src2_are_equal_not_imm){

            //flush the execute stage and pc-4
            flush_buffer(EXECUTE);
            return -1;
        }
    }
}

/*
Abstract: Checks whether the command at the EX buffer is LOAD to regA
and the command at the ID buffer reads from regA.
If so, insterts bubble between the two.
*/
int load_hdu_mem_id()
{

    // Check for the load command at the EXE stage
    if (pipeline.pipeStageState[MEMORY].cmd.opcode != CMD_LOAD)
        return 0;

    // Check whether the dst register in the EXE stage is the same
    // as one of the registers in the ID stage
    if (pipeline.pipeStageState[MEMORY].cmd.dst != pipeline.pipeStageState[DECODE].cmd.src1 &&
        pipeline.pipeStageState[MEMORY].cmd.dst != pipeline.pipeStageState[DECODE].cmd.src2)
        return 0;

    // Add bubble to the pipe
    flush_buffer(EXECUTE);
    return -1;
}


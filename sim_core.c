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
void do_branch_if_needed(void); //todo
// Take Brunch - Flush and load from address
void flush_buffer(int number); //todo - change name to bubble or jump?
void do_flush(int jumpOffset); //todo - need this?
void do_flush_to_i_stages(int i);

// Data-Hazard Detection Unit. Also updates in case of hazard.
void data_hdu(void);
void data_hdu_MEM_EXE(void); // No nop
void data_hdu_WB_EXE(void); // No nop
int load_hdu_Mem_ID(void); // Has nop

// The simulator pipeline
SIM_coreState pipeline;

// The simulator data
int32_t buffer_src_data[SIM_PIPELINE_DEPTH];
int32_t buffer_dst_data[SIM_PIPELINE_DEPTH];
int32_t buffer_pc[SIM_PIPELINE_DEPTH];

int tickNumber = 0;
int brunchAddress = 0;



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

    do_pipe_WB();

    do_pipe_mem();

    do_pipe_execute();
    //todo - add all the hdu
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

/* The function dumps the state to stdout */
void DumpCoreState(SIM_coreState *state){
    //todo
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
        //WB
    }
    //update next pipeBuffer data
    pipeline.pipeStageState[0].cmd = *nextCommand;
    buffer_pc[0] = pipeline.pc;
    //advance the pc to the next command
    pipeline.pc += 4;
    free(nextCommand);
}

//decode the command and store the data in the SIM_cmd struct
void do_pipe_decode(void){
    PipeStageState* decode_pipe_stage = &pipeline.pipeStageState[0];
    SIM_cmd* command = &decode_pipe_stage.cmd;

    //the first value is always red from the memory
    decode_pipe_stage->src1Val = pipeline.regFile[command->src1];

    //in immediate command the second src value is taken from the command
    if (command->isSrc2Imm) {
        decode_pipe_stage->src2Val = command->src2;
    }
    else
    {
        decode_pipe_stage->src2Val = pipeline.regFile[command->src2];
    }
    //advance the pipe
    pipeline.pipeStageState[1].cmd = pipeline.pipeStageState[0].cmd;
    buffer_pc[1] =buffer_pc[0]; //take the pc with you
}

//execute the command
void do_pipe_execute(void){

    // Get the command
    PipeStageState* execute_pipe_state = &pipeline.pipeStageState[1];
    SIM_cmd* command = &execute_pipe_state.cmd;

    switch (command->opcode){

        // R-Type Commands
        // Do ALU commands
        // I'v added the addi and subi command here, is it ok? the number doesn't need to be signed extended?
        case CMD_ADD:
        case CMD_ADDI:
            destData[2] = execute_pipe_state->src1Val + execute_pipe_state->src2Val;
            break;

        case CMD_SUB:
        case CMD_SUBI:
            destData[2] = execute_pipe_state->src1Val - execute_pipe_state->src2Val;
            break;

        // I-Type Commands
            // Calculate memory address
        case CMD_LOAD:
            // calc the address by adding the immediate value. buffer_dst_data now has the address to load from.
            //todo - this is how to calc the address, does it needs to be pc+4?
            buffer_dst_data[2] = execute_pipe_state->src1Val + execute_pipe_state->src2Val;
            break;
        case CMD_STORE:
            buffer_dst_data[2] = command->dst + execute_pipe_state->src2Val;

        case CMD_BR:
        case CMD_BREQ:
        case CMD_BRNEQ:
            buffer_dst_data[2] = command->dst + buffer_pc[1];
        case CMD_HALT: //todo halt?

        default:
            buffer_dst_data[2] = 0;
        break;
    }
    pipeline.pipeStageState[2].cmd = pipeline.pipeStageState[1].cmd;

}

//store, load or branch commands
void do_pipe_mem(void){
    // Get the command
    SIM_cmd* command = &pipeline.pipeStageState[2].cmd;
    PipeStageState* mem_pipe_state = &pipeline.pipeStageState[2];

    int32_t memory_address = buffer_dst_data[2]; //was calculated in the last cycle
    int32_t calculated_branch_address = buffer_dst_data[2];

    switch (command->opcode){
        case CMD_BR:
            // unconditional brunch
            brunchAddress = pipeline.regFile[command->dst];
            break;
        case CMD_BREQ:
            if (mem_pipe_state->src1Val == mem_pipe_state->src2Val){
                brunchAddress = calculated_branch_address;
            }
            break;
        case CMD_BRNEQ:
            if (mem_pipe_state->src1Val != mem_pipe_state->src2Val){
                brunchAddress = calculated_branch_address;
            }
            break;
        case CMD_LOAD:
            //may take more then one cycle, if so all the pipe will wait
            do_pipe_mem_load();
            break;
        case CMD_STORE:
            //will end in one cycle
            SIM_MemDataWrite(memory_address, mem_pipe_state->src1Val);
            break;

        default:
            break;
    }
    pipeline.pipeStageState[3] = pipeline.pipeStageState[2];
    buffer_dst_data[3]= buffer_dst_data[2];
    return 0; //todo - need this?
}


void do_pipe_wb(void){
    SIM_cmd* command = &pipeline.pipeStageState[3].cmd;
    PipeStageState* wb_pipe_state = &pipeline.pipeStageState[3];
    int dst_register = command->dst;

    //if need to WB, write to the buffer.
    switch (wb_pipe_state->cmd){
        case CMD_SUB:
        case CMD_ADD:
        case CMD_SUBI:
        case CMD_ADDI:
        case CMD_LOAD:
            buffer_dst_data[4] = buffer_dst_data[3];
        default:
            //we don't want to write data, so we write 0 to the 0 register
            buffer_dst_data[4] = 0;
            command->dst = 0;
    }
    // if split reg file is on write to the reg file, else this will be done in the begining of the fetch cycle
    if (split_regfile){
        pipeline.regFile[dst_register] = buffer_dst_data[4];
    }

    else{
        //use another buffer for the WB
        pipeline.pipeStageState[4] = pipeline.pipeStageState[3];
    }
}

/*!
 * write to the reg file, use if split reg file is not active
 */
void do_write_to_reg_file(void){
    SIM_cmd* command = &pipeline.pipeStageState[4].cmd;
    PipeStageState* wb_pipe_state = &pipeline.pipeStageState[4];
    int dst_register = command->dst;
    pipeline.regFile[dst_register] = buffer_dst_data[4];

}

/*!
 * flush a pipe stage includes the buffered data and the pc
 */

void flush_buffer(int number){
    PipeStageState* pipe_stage_to_flush = &pipeline.pipeStageState[number];

    pipe_stage_to_flush->src2Val = 0;
    pipe_stage_to_flush->src1Val = 0;
    reset_cmd(pipe_stage_to_flush);
    buffer_dst_data[number] = 0;
    buffer_src_data[number] = 0;
    buffer_pc[number] = 0;
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

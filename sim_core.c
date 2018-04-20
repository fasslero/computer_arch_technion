#include "sim_api.h"

void init_clk_tick(void);
void print_state();


// pipeline Stages
void do_pipe_fetch(void);
void do_pipe_decode(void);
void do_pipe_execute(void);
void do_pipe_mem(void);
// Returns 0 on success, -1 on wait-state
//todo - need this?
int do_pipe_Mem_Load(void);
void do_pipe_WB(void);

// Take Brunch - Flush and load from address
void update_decode_data();
//todo - change name to bubble or jump?
void flush_buffer(int number);
void do_flush(int jumpOffset);

// Data-Hazard Detection Unit. Also updates in case of hazard.
void data_hdu_MEM_EXE(void); // No nop
void data_hdu_WB_EXE(void); // No nop
int load_hdu_Mem_ID(void); // Has nop

// The simulator pipeline
SIM_core_state pipeline;

// The simulator data
int32_t pipeData[SIM_PIPELINE_DEPTH];
int32_t destData[SIM_PIPELINE_DEPTH];
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
}

/*! SIM_CoreClkTick: Update the core simulator's state given one clock cycle.
  This function is expected to update the core pipeline given a clock cycle event.
*/
void SIM_CoreClkTick() {

}

/*! SIM_CoreGetState: Return the current core (pipeline) internal state
    curState: The returned current pipeline state
    The function will return the state of the pipe at the end of a cycle
*/
void SIM_CoreGetState(SIM_coreState *curState) {
}

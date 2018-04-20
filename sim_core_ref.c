/* 046267 Computer Architecture - Spring 2016 - HW #1 */
/* This file should hold your implementation of the CPU pipeline core simulator */

#include "sim_api.h"

void InitClkTick(void);
void printState();


// Pipeline Stages
void DO_Pipe_Fetch(void);
void DO_Pipe_Decode(void);
void DO_Pipe_Execute(void);
void DO_Pipe_Mem(void);
// Returns 0 on success, -1 on wait-state
int DO_Pipe_Mem_Load(void);
void DO_Pipe_WB(void);

// Take Brunch - Flush and load from address
void Update_Decode_Data();
void Flush_Buffer(int number);
void DO_Flush(int jumpOffset);

// Data-Hazard Detection Unit. Also updates in case of hazard.
void Data_HDU_MEM_EXE(void); // No nop
void Data_HDU_WB_EXE(void); // No nop
int Load_HDU_Mem_ID(void); // Has nop

// The simulator pipeline
SIM_coreState pipeline;

// The simulator data
int32_t pipeData[SIM_PIPELINE_DEPTH];
int32_t destData[SIM_PIPELINE_DEPTH];
int tickNumber = 0;
int brunchAddress = 0;

//functions implemantation
int SIM_CoreReset(void)
{
    // Reset the program counter
    pipeline.pc = -4;
    int i;

    // Reset the register file
    for (i=0; i<SIM_REGFILE_SIZE; i++)
    {
        pipeline.regFile[i] = 0;
    }

    // Reset all pipeline stages
    for (i=0; i<SIM_PIPELINE_DEPTH; i++)
    {
        // Reset the current state command
        pipeline.pipeStageState[i].cmd.opcode = 0;
        pipeline.pipeStageState[i].cmd.src1 = 0;
        pipeline.pipeStageState[i].cmd.src2 = 0;
        pipeline.pipeStageState[i].cmd.isSrc2Imm = false;
        pipeline.pipeStageState[i].cmd.dst = 0;

        // Reset the current state values
        pipeline.pipeStageState[i].src1Val = 0;
        pipeline.pipeStageState[i].src2Val = 0;
    }   

    return 0;
}

/*
Name: InitClkTick
Abstract:Manage PC and branch related flush and jump
Inputs: NA
Returns: NA
Error: NA
*/
void InitClkTick(void)
{
    tickNumber++;

    // Update the PC
    pipeline.pc += 4;

    if (brunchAddress != 0)
     {
        DO_Flush(brunchAddress);
        brunchAddress = 0;
    }
}


/*
Name: SIM_CoreClkTick
Abstract:main clk loop
Inputs: NA
Returns: NA
Error: NA
*/
void SIM_CoreClkTick(void)
{       
    //Manage PC and branch related flush
    InitClkTick();
    // Do the Write-Back cycle
    DO_Pipe_WB();
    // Try to move command from stage 3 to stage 4
    // In case the memory had not yet finished, end the cycle
    if (DO_Pipe_Pre_WB() != 0)
        return;

    // Do all other memory stuff
    DO_Pipe_Mem();

    // Do the load hazard check
    // In case of success and load hazard, insert buuble between stages 2 and 3
    if (Load_HDU_Mem_ID() != 0) 
        return;

    // Execute with Mem-Exe forwarding
    Data_HDU_WB_EXE();
    Data_HDU_MEM_EXE();
    DO_Pipe_Execute();

    // Decode with Mem-Id forwarding
    DO_Pipe_Decode();
    
    // Fetch
    DO_Pipe_Fetch();

    printState();

    // The WB operation is done, thus flush the pipe there
    Flush_Buffer(4);
}



void printState() 
{
    printf("\nSimulation on cycle %d. The state is:\n", tickNumber);
    DumpCoreState(&pipeline);
    printf("\n");
}



void SIM_CoreGetState(SIM_coreState *curState)
{   
    *curState = pipeline;
}


/*
Name: DO_Pipe_WB
Abstract: Process the buffer register from stage 3 to stage 4.
Writes the newest register value to the register file
Inputs: NA
Returns: NA
Error: NA
*/
void DO_Pipe_WB() 
{

    // Get the command
    SIM_cmd* command = &pipeline.pipeStageState[4].cmd;
    int dstRegister = command->dst;

    switch (command->opcode) {

        // R-Type Commands
        case CMD_ADD:
        case CMD_SUB:
            pipeline.regFile[dstRegister] = pipeData[4];
            break;

        // LW Command
        case CMD_LOAD:
            pipeline.regFile[dstRegister] = pipeData[4];
            break;

        // If the command doesn't need to write, break      
        default:
            break;
    }
}


int DO_Pipe_Pre_WB()
{
    // If the current MEM command is load, check whether the momory is finished
    if (pipeline.pipeStageState[3].cmd.opcode == CMD_LOAD) {
        // The memory address is already calcualted
        uint32_t memoryAddress = pipeData[3];

        // Allocate the data space
        int32_t* data = (int32_t*)malloc(sizeof(int32_t));

        // Try to read the data. On wait state, return -1
        if (SIM_MemDataRead(memoryAddress, data) == -1)
        {

            // Undo pc update
            pipeline.pc -= 4;
            // The WB operation is done, thus flush the pipe there
            Flush_Buffer(4);
            printState();
            return -1;
        }

        // printf("\nMem address is - %lu - \n", memoryAddress);
        // printf("\nMem val is - %lu  \n",*data);
            
        // Update pipe
        pipeData[3] = *data;

        // Check and update load Hazard data
        if (pipeline.pipeStageState[3].cmd.dst == pipeline.pipeStageState[1].cmd.src1)
            pipeline.pipeStageState[1].src1Val = pipeData[3];
        if (pipeline.pipeStageState[3].cmd.dst == pipeline.pipeStageState[1].cmd.src2)
            pipeline.pipeStageState[1].src2Val = pipeData[3];

        free(data);
    }

    // Move pipe from MEM to WB
    pipeline.pipeStageState[4] = pipeline.pipeStageState[3];
    pipeData[4] = pipeData[3];
    destData[4] = destData[3];
    // Do second write back? 
    DO_Pipe_WB();


    return 0;
}

/*
Name: DO_Pipe_Mem
Abstract: Process the buffer register from stage 2 to stage 3.
Inputs: NA
Returns: Returns 0 on success, -1 on wait-state
Error: NA
*/
void DO_Pipe_Mem()
{

    // Get the command
    SIM_cmd* command = &pipeline.pipeStageState[2].cmd;

    // The memory address is already calcualted
    uint32_t memoryAddress = pipeData[2];

    switch (command->opcode) {

        case CMD_STORE:
            // Store the data
            // printf("\nMem address is - %lu - \n", memoryAddress);
            // printf("\nMem val is - %lu  \n",pipeline.pipeStageState[2].src1Val);
            SIM_MemDataWrite(memoryAddress, pipeline.pipeStageState[2].src1Val);
            break;

        case CMD_BR:

            // Do unconditional brunch
            brunchAddress = pipeline.regFile[command->dst];
            // Return wait-state: stop process more pipe commands

        case CMD_BREQ:

            // Check whether the condition is met. If so, do brunch on next cycle
            if (pipeline.pipeStageState[2].src1Val == pipeline.pipeStageState[2].src2Val)
                brunchAddress =  pipeline.regFile[command->dst];

            break;
            
        case CMD_BRNEQ:

            // Check whether the condition is met. If so, do brunch on next cycle
            if (pipeline.pipeStageState[2].src1Val != pipeline.pipeStageState[2].src2Val)
                brunchAddress =  pipeline.regFile[command->dst];

            break;
    }

    // Use the struct bit-wise copy
    pipeline.pipeStageState[3] = pipeline.pipeStageState[2];
    pipeData[3] = pipeData[2];
    destData[3] = destData[2];
}



/*
Name: DO_Pipe_Execute
Abstract: Populate the pipeData array according to the buffer state 1
and transter the overall data to sage 2
Inputs: NA
Returns: NA
Error: NA
*/
void DO_Pipe_Execute() 
{

    // Get the command
    SIM_cmd* command = &pipeline.pipeStageState[1].cmd;
    int32_t dstValue;

    switch (command->opcode) {

        // R-Type Commands
        // Do ALU commands
        case CMD_ADD:
            pipeData[2] = pipeline.pipeStageState[1].src1Val + pipeline.pipeStageState[1].src2Val;
            break;

        case CMD_SUB:
            pipeData[2] = pipeline.pipeStageState[1].src1Val - pipeline.pipeStageState[1].src2Val;
            break;

        // I-Type Commands
        // Calculate memory address
        case CMD_LOAD:

            if (command->isSrc2Imm)
                pipeData[2] = pipeline.pipeStageState[1].src1Val + command->src2; // Sign-Extend the immediate value.
            else
                pipeData[2] = pipeline.pipeStageState[1].src1Val + pipeline.pipeStageState[1].src2Val;

            break;

        case CMD_STORE:
            
            if (command->isSrc2Imm)
                pipeData[2] = destData[1] + command->src2; // Sign-Extend the immediate value.
            else
                pipeData[2] = destData[1] + pipeline.pipeStageState[1].src2Val;

            break;

        default:

            pipeData[2] = 0;
            break;
    }

    // Move the buffer register from stage 1 to stage 2
    // Use the struct bit-wise copy
    pipeline.pipeStageState[2] = pipeline.pipeStageState[1];
    destData[2] = destData[1];
    // pipeData[2] is up to date
}



/*
Name: DO_Flush
Abstract: Flush the instructions on IF, ID, EXE
Update the PC to PC + offset
Inputs: jumpOffset - The address offset
Returns: NA
Error: NA
*/
void DO_Flush(int jumpOffset)
{
    int i;

    // Flush buffers 0 to 2
    for (i=0; i<=2; i++) {
        Flush_Buffer(i);
    }

    // Update PC. Note that the address should be PC + offset - 4, because the Fetch method!
    pipeline.pc += jumpOffset - 12;
}

/*
Name: Flush_Buffer
Abstract: Flush the instructions on selected buffer
Returns: NA
Error: NA
*/
void Flush_Buffer(int number)
{
    pipeline.pipeStageState[number].cmd.opcode = CMD_NOP;
    pipeline.pipeStageState[number].cmd.src1 = 0;
    pipeline.pipeStageState[number].cmd.src2 = 0;
    pipeline.pipeStageState[number].cmd.isSrc2Imm = false;
    pipeline.pipeStageState[number].cmd.dst = 0;
    pipeline.pipeStageState[number].src1Val = 0;
    pipeline.pipeStageState[number].src2Val = 0;
    pipeData[number] = 0;
    destData[number] = 0;
}

/*
Name: Data_HDU_MEM_EXE
Abstract: Check whether the registers on pipe EXE and ID are the same.
If so, copy the register values from the EXE stage to the ID stage.
Inputs: NA
Returns: NA
Error: NA
*/
void Data_HDU_MEM_EXE() {

    // Get the commands
    SIM_cmd* commandMEM = &pipeline.pipeStageState[3].cmd;
    SIM_cmd* commandEXE = &pipeline.pipeStageState[1].cmd; // The execute BEFORE the do execute!!!

    // Work only if the command in the EXE buffer is write command
    if (commandMEM->opcode != CMD_ADD && commandMEM->opcode != CMD_SUB)
        return;

    // Check the value of register src1
    if (commandMEM->dst == commandEXE->src1)
        pipeline.pipeStageState[1].src1Val = pipeData[3];

    // Check the value of register dst
    if (commandMEM->dst == commandEXE->dst)
        destData[1] = pipeData[3];

    // Check the value of register src2, only if it is not immediate
    if (!commandMEM->isSrc2Imm && !commandEXE->isSrc2Imm) {
        if (commandMEM->dst == commandEXE->src2)
            pipeline.pipeStageState[1].src2Val = pipeData[3];
    }
}



/*
Name: Load_HDU_Mem_ID
Abstract: Checks whether the command at the EX buffer is LOAD to regA
and the command at the ID buffer reads from regA.
If so, insterts bubble between the two.
Inputs: NA
Returns: NA
Error: NA
*/
int Load_HDU_Mem_ID() 
{

    // Check for the load command at the EXE stage
    if (pipeline.pipeStageState[3].cmd.opcode != CMD_LOAD)
        return 0;

    // Check whether the dst register in the EXE stage is the same
    // as one of the registers in the ID stage
    if (pipeline.pipeStageState[3].cmd.dst != pipeline.pipeStageState[1].cmd.src1 &&
        pipeline.pipeStageState[3].cmd.dst != pipeline.pipeStageState[1].cmd.src2)
        return 0;
    
    // Add bubble to the pipe
    Flush_Buffer(2);
    Update_Decode_Data();
    pipeline.pc -= 4;
    printState();
    return -1;
}



/*
Name: DO_Pipe_Fetch
Abstract: Fetchs the command from the instruction registers
and put it in the pipe
Inputs: NA
Returns: NA
Error: NA
*/
void DO_Pipe_Fetch(void)
{
    // asign new mem for next command and read it from IM.
    SIM_cmd* nextCommand = (SIM_cmd*)malloc(sizeof(SIM_cmd)); 
    SIM_MemInstRead(pipeline.pc, nextCommand);

    //update next pipeBuffer data
    pipeline.pipeStageState[0].cmd = *nextCommand;

    free(nextCommand);
}



/*
Name: DO_Pipe_Decode
Abstract: Decoding the instruction and transfer it to the next pipe stage
Inputs: NA
Returns: NA
Error: NA
*/

void DO_Pipe_Decode(void)
{
    //check command type (R/I - type) and retrive RegFile data accordingly
    pipeline.pipeStageState[1].cmd = pipeline.pipeStageState[0].cmd;
    Update_Decode_Data();
}


/*
Name: Update_Decode_Data
Abstract: Decoding the instruction and transfer it to the next pipe stage
Inputs: NA
Returns: NA
Error: NA
*/
void Update_Decode_Data(void) {
    SIM_cmd* currentCMD = &pipeline.pipeStageState[1].cmd;
    //I-Type
    if(currentCMD->isSrc2Imm)
    {
        pipeline.pipeStageState[1].src1Val = pipeline.regFile[currentCMD->src1];
        pipeline.pipeStageState[1].src2Val = currentCMD->src2;  
    }

    //R-Type
    else
    {
        pipeline.pipeStageState[1].src1Val = pipeline.regFile[currentCMD->src1];
        pipeline.pipeStageState[1].src2Val = pipeline.regFile[currentCMD->src2];    
    }

    destData[1] = pipeline.regFile[currentCMD->dst];
}

/*
Name: Data_HDU_Mem_ID
Abstract: Checks whether the command at the Mem buffer is LOAD to regA
and the command at the ID buffer reads from mem stage to id.
If so, it forwards the value f
Inputs: NA
Returns: NA
Error: NA
*/
void Data_HDU_WB_EXE(void)
{
    SIM_cmd* cmdID = &pipeline.pipeStageState[1].cmd; // The execute BEFORE the do execute!!!
    SIM_cmd* cmdMEM = &pipeline.pipeStageState[4].cmd;

    switch(cmdMEM->opcode)
    {
        case CMD_ADD:
        case CMD_SUB:
            if(cmdID->src1 == cmdMEM->dst)
            {   
                pipeline.pipeStageState[1].src1Val = pipeData[4];
            }

            if(cmdID->dst == cmdMEM->dst)
                destData[1] = pipeData[4];

            if (!cmdMEM->isSrc2Imm && !cmdID->isSrc2Imm) {
                if(cmdID->src2 == cmdMEM->dst)
                    pipeline.pipeStageState[1].src2Val = pipeData[4];
            }
    }
}

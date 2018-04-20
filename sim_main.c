/* 046267 Computer Architecture - Spring 2017 - HW #1               */
/* Main program for simulation environment testing                  */
/* Usage: ./sim_main <image filename> <cycles> [-s|-f]           */

#include <stdlib.h>
#include <stdio.h>
#include "sim_api.h"

bool forwarding;
bool split_regfile;

void DumpCoreState(SIM_coreState *state) {
    int i;
    SIM_cmd *curCmd;
    char const *curCmdStr;

    PipeStageState pipeStageState[SIM_PIPELINE_DEPTH];

    printf("PC = 0x%X\n", state->pc);
    printf("Register file:\n");
    for (i = 0; i < SIM_REGFILE_SIZE; ++i)
        printf("\tR%d = 0x%X", i, state->regFile[i]);
    printf("\nCommand at each pipe stage:\n");
    for (i = 0; i < SIM_PIPELINE_DEPTH; ++i)
    {
        curCmd = &state->pipeStageState[i].cmd;
        if ((curCmd->opcode > CMD_MAX) || (curCmd->opcode < 0))
            curCmdStr = "<Invalid Cmd.>";
        else
            curCmdStr = cmdStr[curCmd->opcode];
        printf("\t%s : %s $%d , $%d(=0x%X) , %s%d(=0x%X)\n", pipeStageStr[i],
               curCmdStr, curCmd->dst, curCmd->src1,
               state->pipeStageState[i].src1Val,
               (curCmd->isSrc2Imm ? "" : "$"), curCmd->src2,
               state->pipeStageState[i].src2Val);
    }
}

bool DetectHALT(SIM_coreState *state) {
    SIM_cmd *curCmd;
    curCmd = &state->pipeStageState[SIM_PIPELINE_DEPTH - 1].cmd;
    if (curCmd->opcode == CMD_HALT) {
        return true; 
    } else {
        return false; 
    }
}

int main(int argc, char const *argv[]) {
    int i, simDuration;
    forwarding = false;
    split_regfile = false;
    char const *memFname = argv[1];
    char const *simDurationStr = argv[2];

    SIM_coreState curState;

    if (argc < 3) {
            fprintf(stderr, "Usage: %s <memory image filename> <number of cycles to run> [-s|-f]\n", argv[0]);
        exit(1);
    }
    if (argc > 3) {
        if (strcmp(argv[3], "-s") == 0) {
            split_regfile = true;
            printf("Split RegFile enabled\n");
        } else if (strcmp(argv[3], "-f") == 0) {
            printf("Split RegFile enabled\n");
            printf("Forwarding enabled\n");
            split_regfile = true;
            forwarding = true;
        } else {
            fprintf(stderr, "Usage: %s <memory image filename> <number of cycles to run> [-s|-f]\n", argv[0]);
        }
    }


    /* Initialized simulation modules */
    if (SIM_MemReset(memFname) != 0) {
        fprintf(stderr, "Failed initializing memory simulator!\n");
        exit(2);
    }

    printf("Resetting core...\n");
    if (SIM_CoreReset() != 0) {
        fprintf(stderr, "Failed resetting core!\n");
        exit(3);
    }

    /* Running simulation */
    simDuration = atoi(simDurationStr);
    if (simDuration <= 0) {
        fprintf(stderr, "Invalid simulation duration argument: %s\n",
                simDurationStr);
        exit(4);
    }

    printf("Running simulation for %d cycles\n", simDuration);
    printf("Simulation on cycle %d. The state is:\n", 0);
    SIM_CoreGetState(&curState);
    DumpCoreState(&curState);
    for (i = 0; i < simDuration; i++) {
        SIM_CoreClkTick();
        SIM_MemClkTick();
        printf("\n\nSimulation on cycle %d. The state is:\n", i+1);
        SIM_CoreGetState(&curState);
        DumpCoreState(&curState);
        bool is_halt = DetectHALT(&curState);
        if (is_halt) {
            printf("Program successfully ran for %d cycles\n", i + 1);
            exit(0);
        }
    }


    return 0;
}

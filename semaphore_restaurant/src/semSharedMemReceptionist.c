/**
 *  \file semSharedReceptionist.c (implementation file)
 *
 *  \brief Problem name: Restaurant
 *
 *  Synchronization based on semaphores and shared memory.
 *  Implementation with SVIPC.
 *
 *  Definition of the operations carried out by the receptionist:
 *     \li waitForGroup
 *     \li provideTableOrWaitingRoom
 *     \li receivePayment
 *
 *  \author Nuno Lau - December 2023
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "probConst.h"
#include "probDataStruct.h"
#include "logging.h"
#include "sharedDataSync.h"
#include "semaphore.h"
#include "sharedMemory.h"

/** \brief logging file name */
static char nFic[51];

/** \brief shared memory block access identifier */
static int shmid;

/** \brief semaphore set access identifier */
static int semgid;

/** \brief pointer to shared memory region */
static SHARED_DATA *sh;

/* constants for groupRecord */
#define TOARRIVE 0
#define WAIT 1
#define ATTABLE 2
#define DONE 3

/** \brief receptioninst view on each group evolution (useful to decide table binding) */
static int groupRecord[MAXGROUPS];

/** \brief receptionist waits for next request */
static request waitForGroup();

/** \brief receptionist waits for next request */
static void provideTableOrWaitingRoom(int n);

/** \brief receptionist receives payment */
static void receivePayment(int n);

/**
 *  \brief Main program.
 *
 *  Its role is to generate the life cycle of one of intervening entities in the problem: the receptionist.
 */
int main(int argc, char *argv[])
{
    int key;    /*access key to shared memory and semaphore set */
    char *tinp; /* numerical parameters test flag */

    /* validation of command line parameters */
    if (argc != 4)
    {
        freopen("error_RT", "a", stderr);
        fprintf(stderr, "Number of parameters is incorrect!\n");
        return EXIT_FAILURE;
    }
    else
    {
        freopen(argv[3], "w", stderr);
        setbuf(stderr, NULL);
    }

    strcpy(nFic, argv[1]);
    key = (unsigned int)strtol(argv[2], &tinp, 0);
    if (*tinp != '\0')
    {
        fprintf(stderr, "Error on the access key communication!\n");
        return EXIT_FAILURE;
    }

    /* connection to the semaphore set and the shared memory region and mapping the shared region onto the
       process address space */
    if ((semgid = semConnect(key)) == -1)
    {
        perror("error on connecting to the semaphore set");
        return EXIT_FAILURE;
    }
    if ((shmid = shmemConnect(key)) == -1)
    {
        perror("error on connecting to the shared memory region");
        return EXIT_FAILURE;
    }
    if (shmemAttach(shmid, (void **)&sh) == -1)
    {
        perror("error on mapping the shared region on the process address space");
        return EXIT_FAILURE;
    }

    /* initialize random generator */
    srandom((unsigned int)getpid());

    /* initialize internal receptionist memory */
    int g;
    for (g = 0; g < sh->fSt.nGroups; g++)
    {
        groupRecord[g] = TOARRIVE;
    }

    /* simulation of the life cycle of the receptionist */
    int nReq = 0;
    request req;
    while (nReq < sh->fSt.nGroups * 2)
    {
        req = waitForGroup();
        switch (req.reqType)
        {
        case TABLEREQ:
            provideTableOrWaitingRoom(req.reqGroup); // TODO param should be groupid
            break;
        case BILLREQ:
            receivePayment(req.reqGroup);
            break;
        }
        nReq++;
    }

    /* unmapping the shared region off the process address space */
    if (shmemDettach(sh) == -1)
    {
        perror("error on unmapping the shared region off the process address space");
        return EXIT_FAILURE;
        ;
    }

    return EXIT_SUCCESS;
}

/**
 *  \brief decides table to occupy for group n or if it must wait.
 *
 *  Checks current state of tables and groups in order to decide table or wait.
 *
 *  \return table id or -1 (in case of wait decision)
 */
int decideTableOrWait(int n)
{

    // ID da mesa em que o grupo se irá sentar
    int tableID = -1;
    // Numero de mesa a atribuir (0 ou 1) e váriavel para saber se a mesa esta em uso
    int numTable, notInUse;

    // Ciclos para verificar se existem mesas disponiveis ou nao
    for (numTable = 0; numTable < NUMTABLES; numTable++){
        notInUse = 1;
        for (int i = 0; i < sh->fSt.nGroups; i++){
            if (sh->fSt.assignedTable[i] == numTable){
                notInUse = 0;
                break;
            }
        }
        if (notInUse){
            // Se existirem mesas disponiveis, atribuir ao ID o número disponivel (0 ou 1)
            tableID = numTable;
            break;
        }
    }

    return tableID;

}

/**
 *  \brief called when a table gets vacant and there are waiting groups
 *         to decide which group (if any) should occupy it.
 *
 *  Checks current state of tables and groups in order to decide group.
 *
 *  \return group id or -1 (in case of wait decision)
 */
static int decideNextGroup()
{

    // Próximo grupo a ser atendido
    int nextGroup = -1;

    // Ciclo para verificar se existem grupos à espera
    for (int i = 0; i < sh->fSt.nGroups; i++){
        if (groupRecord[i] == WAIT){
            // Se existirem grupos a espera, atribuir ao nextGroup o número do grupo
            nextGroup = i;
            break;
        }
    }

    return nextGroup;

}

/**
 *  \brief receptionist waits for next request
 *
 *  Receptionist updates state and waits for request from group, then reads request,
 *  and signals availability for new request.
 *  The internal state should be saved.
 *
 *  \return request submitted by group
 */
static request waitForGroup()
{
    request ret;

    if (semDown(semgid, sh->mutex) == -1){                                                  /* enter critical region */
        perror("error on the down operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }

    // Aualizar e guardar estado do rececionista para esperar por um pedido
    sh->fSt.st.receptionistStat = WAIT_FOR_REQUEST;
    saveState(nFic, &sh->fSt);

    if (semUp(semgid, sh->mutex) == -1){                                             /* exit critical region */
        perror("error on the up operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }

    // Bloquear o rececionista até que um grupo faça um pedido
    if (semDown(semgid, sh->receptionistReq) == -1){
        perror("error on the down operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }

    if (semDown(semgid, sh->mutex) == -1){                                                  /* enter critical region */
        perror("error on the down operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }

    // Atualizar a variavel ret com o pedido do grupo e reiniciar o pedido do rececionista, guardando-o
    ret = sh->fSt.receptionistRequest;
    sh->fSt.receptionistRequest.reqType = -1;
    sh->fSt.receptionistRequest.reqGroup = -1;
    saveState(nFic, &sh->fSt);

    if (semUp(semgid, sh->mutex) == -1){                                             /* exit critical region */
        perror("error on the up operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }

    // Aualizar o semáforo para sinalizar que o rececionista pode receber um pedido
    if (semUp(semgid, sh->receptionistRequestPossible) == -1){
        perror("error on the up operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }
    return ret;
}

/**
 *  \brief receptionist decides if group should occupy table or wait
 *
 *  Receptionist updates state and then decides if group occupies table
 *  or waits. Shared (and internal) memory may need to be updated.
 *  If group occupies table, it must be informed that it may proceed.
 *  The internal state should be saved.
 *
 */
static void provideTableOrWaitingRoom(int n)
{
    if (semDown(semgid, sh->mutex) == -1){                                                  /* enter critical region */
        perror("error on the down operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }

    // Atualizar e guardar o estado do rececionista para atribuição de mesas
    sh->fSt.st.receptionistStat = ASSIGNTABLE;
    saveState(nFic, &sh->fSt);

    // Chamar a função decideTableOrWait para saber se existem mesas disponiveis
    int table = decideTableOrWait(n); 

    // Verificar se existem mesas disponiveis
    if (table != -1){
        // Se existirem mesas disponiveis, atribuir ao grupo a mesa disponivel e atualizar o estado deste
        sh->fSt.assignedTable[n] = table;
        groupRecord[n] = ATTABLE;
        // Sinalizar que o grupo pode prosseguir
        if (semUp(semgid, sh->waitForTable[n]) == -1){
            perror("error on the up operation for semaphore access (WT)");
            exit(EXIT_FAILURE);
        }
    }
    else{
        // Se não existirem mesas disponiveis, atualizar o estado do grupo para WAIT e incrementar o numero de grupos à espera
        sh->fSt.groupsWaiting++;
        groupRecord[n] = WAIT;
    }

    if (semUp(semgid, sh->mutex) == -1){                                             /* exit critical region */
        perror("error on the up operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }
}

/**
 *  \brief receptionist receives payment
 *
 *  Receptionist updates its state and receives payment.
 *  If there are waiting groups, receptionist should check if table that just became
 *  vacant should be occupied. Shared (and internal) memory should be updated.
 *  The internal state should be saved.
 *
 */

static void receivePayment(int n)
{
    if (semDown(semgid, sh->mutex) == -1){                                                  /* enter critical region */
        perror("error on the down operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }

    // Atualizar e guardar o estado do rececionista para receber pagamento
    sh->fSt.st.receptionistStat = RECVPAY;
    saveState(nFic, &sh->fSt);

    // Libertar a mesa que o grupo estava a ocupar
    if (semUp(semgid, sh->tableDone[sh->fSt.assignedTable[n]]) == -1){
        perror("error on the up operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }

    // Sinalizar que a mesa está disponivel e atualizar o estado do grupo
    sh->fSt.assignedTable[n] = -1;
    groupRecord[n] = DONE;
    saveState(nFic, &sh->fSt);

    if (semUp(semgid, sh->mutex) == -1){                                             /* exit critical region */
        perror("error on the up operation for semaphore access (WT)");
        exit(EXIT_FAILURE);
    }

    // Chamar a função decideNextGroup para saber se existem grupos à espera
    int group = decideNextGroup();

    // Verificar se existem grupos à espera
    if (group != -1){
        // Se existirem grupos à espera, atribuir uma mesa ao grupo e atualizar o estado deste
        provideTableOrWaitingRoom(group);
        sh->fSt.groupsWaiting--;
    }

    // Se não existirem grupos à espera o programa termina
}
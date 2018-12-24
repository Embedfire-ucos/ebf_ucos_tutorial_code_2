/*
***********************************************************************************************************************
*                                                      uC/OS-III
*                                                 The Real-Time Kernel
*
*                                  (c) Copyright 2009-2012; Micrium, Inc.; Weston, FL
*                           All rights reserved.  Protected by international copyright laws.
*
*                                                   TICK MANAGEMENT
*
* File    : OS_TICK.C
* By      : JJL
* Version : V3.03.01
*
* LICENSING TERMS:
* ---------------
*           uC/OS-III is provided in source form for FREE short-term evaluation, for educational use or 
*           for peaceful research.  If you plan or intend to use uC/OS-III in a commercial application/
*           product then, you need to contact Micrium to properly license uC/OS-III for its use in your 
*           application/product.   We provide ALL the source code for your convenience and to help you 
*           experience uC/OS-III.  The fact that the source is provided does NOT mean that you can use 
*           it commercially without paying a licensing fee.
*
*           Knowledge of the source code may NOT be used to develop a similar product.
*
*           Please help us continue to provide the embedded community with the finest software available.
*           Your honesty is greatly appreciated.
*
*           You can contact us at www.micrium.com, or by phone at +1 (954) 217-2036.
************************************************************************************************************************
*/

#define  MICRIUM_SOURCE
#include <os.h>

#ifdef VSC_INCLUDE_SOURCE_FILE_NAMES
const  CPU_CHAR  *os_tick__c = "$Id: $";
#endif

/*
************************************************************************************************************************
*                                                  LOCAL PROTOTYPES
************************************************************************************************************************
*/


/*
************************************************************************************************************************
*                                                      TICK TASK
*
* Description: This task is internal to uC/OS-III and is triggered by the tick interrupt.
*
* Arguments  : p_arg     is an argument passed to the task when the task is created (unused).
*
* Returns    : none
*
* Note(s)    : This function is INTERNAL to uC/OS-III and your application should not call it.
************************************************************************************************************************
*/

void  OS_TickTask (void  *p_arg)
{
    OS_ERR  err;
    CPU_TS  ts;


    p_arg = p_arg;                                           //预防编译警告，没有实际意义

    while (DEF_ON) {                                         //循环运行
        (void)OSTaskSemPend((OS_TICK  )0,                    //等待来自时基中断的信号量，接收到信号量后继续运行
                            (OS_OPT   )OS_OPT_PEND_BLOCKING,
                            (CPU_TS  *)&ts,
                            (OS_ERR  *)&err);            
        if (err == OS_ERR_NONE) {                            //如果上面接受的信号量没有错误
            if (OSRunning == OS_STATE_OS_RUNNING) {          //如果操作系统正在运行
                OS_TickListUpdate();                         //更新所有任务的时间等待时间（如延时、超时等）
            }
        }
    }
}

/*$PAGE*/
/*
************************************************************************************************************************
*                                                 INITIALIZE TICK TASK
*
* Description: This function is called by OSInit() to create the tick task.
*
* Arguments  : p_err   is a pointer to a variable that will hold the value of an error code:
*
*                          OS_ERR_TICK_STK_INVALID   if the pointer to the tick task stack is a NULL pointer
*                          OS_ERR_TICK_STK_SIZE      indicates that the specified stack size
*                          OS_ERR_PRIO_INVALID       if the priority you specified in the configuration is invalid
*                                                      (There could be only one task at the Idle Task priority)
*                                                      (Maybe the priority you specified is higher than OS_CFG_PRIO_MAX-1
*                          OS_ERR_??                 other error code returned by OSTaskCreate()
*
* Returns    : none
*
* Note(s)    : This function is INTERNAL to uC/OS-III and your application should not call it.
************************************************************************************************************************
*/

void  OS_TickTaskInit (OS_ERR  *p_err)
{
#ifdef OS_SAFETY_CRITICAL
    if (p_err == (OS_ERR *)0) {
        OS_SAFETY_CRITICAL_EXCEPTION();
        return;
    }
#endif

    OSTickCtr         = (OS_TICK)0u;                        /* Clear the tick counter                                 */

    OSTickTaskTimeMax = (CPU_TS)0u;


    OS_TickListInit();                                      /* Initialize the tick list data structures               */

                                                            /* ---------------- CREATE THE TICK TASK ---------------- */
    if (OSCfg_TickTaskStkBasePtr == (CPU_STK *)0) {
       *p_err = OS_ERR_TICK_STK_INVALID;
        return;
    }

    if (OSCfg_TickTaskStkSize < OSCfg_StkSizeMin) {
       *p_err = OS_ERR_TICK_STK_SIZE_INVALID;
        return;
    }

    if (OSCfg_TickTaskPrio >= (OS_CFG_PRIO_MAX - 1u)) {     /* Only one task at the 'Idle Task' priority              */
       *p_err = OS_ERR_TICK_PRIO_INVALID;
        return;
    }

    OSTaskCreate((OS_TCB     *)&OSTickTaskTCB,
                 (CPU_CHAR   *)((void *)"uC/OS-III Tick Task"),
                 (OS_TASK_PTR )OS_TickTask,
                 (void       *)0,
                 (OS_PRIO     )OSCfg_TickTaskPrio,
                 (CPU_STK    *)OSCfg_TickTaskStkBasePtr,
                 (CPU_STK_SIZE)OSCfg_TickTaskStkLimit,
                 (CPU_STK_SIZE)OSCfg_TickTaskStkSize,
                 (OS_MSG_QTY  )0u,
                 (OS_TICK     )0u,
                 (void       *)0,
                 (OS_OPT      )(OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR | OS_OPT_TASK_NO_TLS),
                 (OS_ERR     *)p_err);
}

/*$PAGE*/
/*
************************************************************************************************************************
*                                               INITIALIZE THE TICK LIST
*
* Description: This function initializes the tick handling data structures of uC/OS-III.
*
* Arguments  : none
*
* Returns    : None
*
* Note(s)    : This function is INTERNAL to uC/OS-III and your application MUST NOT call it.
************************************************************************************************************************
*/

void  OS_TickListInit (void)
{
    OS_TICK_SPOKE_IX   i;
    OS_TICK_SPOKE     *p_spoke;



    for (i = 0u; i < OSCfg_TickWheelSize; i++) {
        p_spoke                = (OS_TICK_SPOKE *)&OSCfg_TickWheel[i];
        p_spoke->FirstPtr      = (OS_TCB        *)0;
        p_spoke->NbrEntries    = (OS_OBJ_QTY     )0u;
        p_spoke->NbrEntriesMax = (OS_OBJ_QTY     )0u;
    }
}

/*$PAGE*/
/*
************************************************************************************************************************
*                                                ADD TASK TO TICK LIST
*
* Description: This function is called to place a task in a list of task waiting for either time to expire or waiting to
*              timeout on a pend call.
*
* Arguments  : p_tcb          is a pointer to the OS_TCB of the task to add to the tick list
*              -----
*
*              time           represents either the 'match' value of OSTickCtr or a relative time from the current
*                             value of OSTickCtr as specified by the 'opt' argument..
*
*                             relative when 'opt' is set to OS_OPT_TIME_DLY
*                             relative when 'opt' is set to OS_OPT_TIME_TIMEOUT
*                             match    when 'opt' is set to OS_OPT_TIME_MATCH
*                             periodic when 'opt' is set to OS_OPT_TIME_PERIODIC
*
*              opt            is an option specifying how to calculate time.  The valid values are:
*              ---
*                                 OS_OPT_TIME_DLY
*                                 OS_OPT_TIME_TIMEOUT
*                                 OS_OPT_TIME_PERIODIC
*                                 OS_OPT_TIME_MATCH
*
*              p_err          is a pointer to a variable that will contain an error code returned by this function.
*              -----
*                                 OS_ERR_NONE           the call was successful and the time delay was scheduled.
*                                 OS_ERR_TIME_ZERO_DLY  if delay is zero or already occurred.
*
* Returns    : None
*
* Note(s)    : 1) This function is INTERNAL to uC/OS-III and your application MUST NOT call it.
*
*              2) This function is assumed to be called with interrupts disabled.
************************************************************************************************************************
*/

void  OS_TickListInsert (OS_TCB   *p_tcb,
                         OS_TICK   time,
                         OS_OPT    opt,
                         OS_ERR   *p_err)
{
    OS_TICK            tick_delta;
    OS_TICK            tick_next;
    OS_TICK_SPOKE     *p_spoke;
    OS_TCB            *p_tcb0;
    OS_TCB            *p_tcb1;
    OS_TICK_SPOKE_IX   spoke;



    if (opt == OS_OPT_TIME_MATCH) {                              /* Task time is absolute.                            */
        tick_delta = time - OSTickCtr - 1u;
        if (tick_delta > OS_TICK_TH_RDY) {                       /* If delay already occurred, ...                    */
            p_tcb->TickCtrMatch = (OS_TICK        )0u;
            p_tcb->TickRemain   = (OS_TICK        )0u;
            p_tcb->TickSpokePtr = (OS_TICK_SPOKE *)0;
           *p_err               =  OS_ERR_TIME_ZERO_DLY;         /* ... do NOT delay.                                 */
            return;
        }
        p_tcb->TickCtrMatch = time;
        p_tcb->TickRemain   = tick_delta + 1u;

    } else if (time > (OS_TICK)0u) {
        if (opt == OS_OPT_TIME_PERIODIC) {                       /* Task time is periodic.                            */
            tick_next  = p_tcb->TickCtrPrev + time;
            tick_delta = tick_next - OSTickCtr - 1u;
            if (tick_delta < time) {                             /* If next periodic delay did NOT already occur, ... */
                p_tcb->TickCtrMatch = tick_next;                 /* ... set  next  periodic delay;                ... */
            } else {
                p_tcb->TickCtrMatch = OSTickCtr + time;          /* ... else reset periodic delay.                    */
            }
            p_tcb->TickRemain   = p_tcb->TickCtrMatch - OSTickCtr;
            p_tcb->TickCtrPrev  = p_tcb->TickCtrMatch;

        } else {                                                 /* Task time is relative to current.                 */
            p_tcb->TickCtrMatch = OSTickCtr + time;
            p_tcb->TickRemain   = time;
        }

    } else {                                                     /* Zero time delay; ...                              */
        p_tcb->TickCtrMatch = (OS_TICK        )0u;
        p_tcb->TickRemain   = (OS_TICK        )0u;
        p_tcb->TickSpokePtr = (OS_TICK_SPOKE *)0;
       *p_err               =  OS_ERR_TIME_ZERO_DLY;             /* ... do NOT delay.                                 */
        return;
    }


    spoke   = (OS_TICK_SPOKE_IX)(p_tcb->TickCtrMatch % OSCfg_TickWheelSize); //使用哈希算法（取余）来决定任务存于 OSCfg_TickWheel 数组
    p_spoke = &OSCfg_TickWheel[spoke];                                       //的哪个元素（节拍列表），与查找到期任务时对应，可方便查找。

    if (p_spoke->NbrEntries == (OS_OBJ_QTY)0u) {                 /* First entry in the spoke                          */
        p_tcb->TickNextPtr   = (OS_TCB   *)0;
        p_tcb->TickPrevPtr   = (OS_TCB   *)0;
        p_spoke->FirstPtr    =  p_tcb;
        p_spoke->NbrEntries  = (OS_OBJ_QTY)1u;
    } else {
        p_tcb1     = p_spoke->FirstPtr;                          /* Point to current first TCB in the list            */
        while (p_tcb1 != (OS_TCB *)0) {
            p_tcb1->TickRemain = p_tcb1->TickCtrMatch            /* Compute time remaining of current TCB in list     */
                               - OSTickCtr;
            if (p_tcb->TickRemain > p_tcb1->TickRemain) {        /* Do we need to insert AFTER current TCB in list?   */
                if (p_tcb1->TickNextPtr != (OS_TCB *)0) {        /* Yes, are we pointing at the last TCB in the list? */
                    p_tcb1               =  p_tcb1->TickNextPtr; /* No,  Point to next TCB in the list                */
                } else {
                    p_tcb->TickNextPtr   = (OS_TCB *)0;
                    p_tcb->TickPrevPtr   =  p_tcb1;
                    p_tcb1->TickNextPtr  =  p_tcb;               /* Yes, TCB to add is now new last entry in the list */
                    p_tcb1               = (OS_TCB *)0;          /* Break loop                                        */
                }
            } else {                                             /* Insert before the current TCB                     */
                if (p_tcb1->TickPrevPtr == (OS_TCB *)0) {        /* Are we inserting before the first TCB?            */
                    p_tcb->TickPrevPtr   = (OS_TCB *)0;
                    p_tcb->TickNextPtr   =  p_tcb1;
                    p_tcb1->TickPrevPtr  =  p_tcb;
                    p_spoke->FirstPtr    =  p_tcb;
                } else {                                         /* Insert in between 2 TCBs already in the list      */
                    p_tcb0               =  p_tcb1->TickPrevPtr;
                    p_tcb->TickPrevPtr   =  p_tcb0;
                    p_tcb->TickNextPtr   =  p_tcb1;
                    p_tcb0->TickNextPtr  =  p_tcb;
                    p_tcb1->TickPrevPtr  =  p_tcb;
                }
                p_tcb1 = (OS_TCB *)0;                            /* Break loop                                        */
            }
        }
        p_spoke->NbrEntries++;
    }
    if (p_spoke->NbrEntriesMax < p_spoke->NbrEntries) {          /* Keep track of maximum # of entries in each spoke  */
        p_spoke->NbrEntriesMax = p_spoke->NbrEntries;
    }
    p_tcb->TickSpokePtr = p_spoke;                               /* Link back to tick spoke                           */
   *p_err               = OS_ERR_NONE;
}

/*$PAGE*/
/*
************************************************************************************************************************
*                                         REMOVE A TASK FROM THE TICK LIST
*
* Description: This function is called to remove a task from the tick list
*
* Arguments  : p_tcb          Is a pointer to the OS_TCB to remove.
*              -----
*
* Returns    : none
*
* Note(s)    : 1) This function is INTERNAL to uC/OS-III and your application MUST NOT call it.
*
*              2) This function is assumed to be called with interrupts disabled.
************************************************************************************************************************
*/

void  OS_TickListRemove (OS_TCB  *p_tcb)
{
    OS_TICK_SPOKE  *p_spoke;
    OS_TCB         *p_tcb1;
    OS_TCB         *p_tcb2;



    p_spoke = p_tcb->TickSpokePtr;
    if (p_spoke != (OS_TICK_SPOKE *)0) {                              /* Confirm that task is in tick list            */
        p_tcb->TickRemain = (OS_TICK)0u;
        if (p_spoke->FirstPtr == p_tcb) {                             /* Is timer to remove at the beginning of list? */
            p_tcb1            = (OS_TCB *)p_tcb->TickNextPtr;         /* Yes                                          */
            p_spoke->FirstPtr = p_tcb1;
            if (p_tcb1 != (OS_TCB *)0) {
                p_tcb1->TickPrevPtr = (OS_TCB *)0;
            }
        } else {
            p_tcb1              = p_tcb->TickPrevPtr;                 /* No, remove timer from somewhere in the list  */
            p_tcb2              = p_tcb->TickNextPtr;
            p_tcb1->TickNextPtr = p_tcb2;
            if (p_tcb2 != (OS_TCB *)0) {
                p_tcb2->TickPrevPtr = p_tcb1;
            }
        }
        p_tcb->TickNextPtr  = (OS_TCB        *)0;
        p_tcb->TickPrevPtr  = (OS_TCB        *)0;
        p_tcb->TickSpokePtr = (OS_TICK_SPOKE *)0;
        p_tcb->TickCtrMatch = (OS_TICK        )0u;
        p_spoke->NbrEntries--;
    }
}

/*$PAGE*/
/*
************************************************************************************************************************
*                                              RESET TICK LIST PEAK DETECTOR
*
* Description: This function is used to reset the peak detector for the number of entries in each spoke.
*
* Arguments  : void
*
* Returns    : none
*
* Note(s)    : This function is INTERNAL to uC/OS-III and your application should not call it.
************************************************************************************************************************
*/


void  OS_TickListResetPeak (void)
{
    OS_TICK_SPOKE_IX   i;
    OS_TICK_SPOKE     *p_spoke;



    for (i = 0u; i < OSCfg_TickWheelSize; i++) {
        p_spoke                = (OS_TICK_SPOKE *)&OSCfg_TickWheel[i];
        p_spoke->NbrEntriesMax = (OS_OBJ_QTY     )0u;
    }
}

/*$PAGE*/
/*
************************************************************************************************************************
*                                                UPDATE THE TICK LIST
*
* Description: This function is called when a tick occurs and determines if the timeout waiting for a kernel object has
*              expired or a delay has expired.
*
* Arguments  : non
*
* Returns    : none
*
* Note(s)    : 1) This function is INTERNAL to uC/OS-III and your application MUST NOT call it.
************************************************************************************************************************
*/

void  OS_TickListUpdate (void)
{
    CPU_BOOLEAN        done;
    OS_TICK_SPOKE     *p_spoke;
    OS_TCB            *p_tcb;
    OS_TCB            *p_tcb_next;
    OS_TICK_SPOKE_IX   spoke;
    CPU_TS             ts_start;
    CPU_TS             ts_end;
    CPU_SR_ALLOC();                                                   //使用到临界段（在关/开中断时）时必需该宏，该宏声明和定义一个局部变
                                                                      //量，用于保存关中断前的 CPU 状态寄存器 SR（临界段关中断只需保存SR）
                                                                      //，开中断时将该值还原。
    OS_CRITICAL_ENTER();                                              //进入临界段
    ts_start = OS_TS_GET();                                           //获取 OS_TickTask() 任务的起始时间戳
    OSTickCtr++;                                                      //时钟节拍数自加
    spoke    = (OS_TICK_SPOKE_IX)(OSTickCtr % OSCfg_TickWheelSize);   //使用哈希算法（取余）缩小查找到期任务位于 OSCfg_TickWheel 数组的
    p_spoke  = &OSCfg_TickWheel[spoke];                               //哪个元素（一个节拍列表），与任务插入数组时对应，下面只操作该列表。
    p_tcb    = p_spoke->FirstPtr;                                     //获取节拍列表的首个任务控制块的地址
    done     = DEF_FALSE;                                             //使下面 while 体得到运行
    while (done == DEF_FALSE) {
        if (p_tcb != (OS_TCB *)0) {                                   //如果该任务不空（存在）
            p_tcb_next = p_tcb->TickNextPtr;                          //获取该列表中紧邻该任务的下一个任务控制块的地址  
            switch (p_tcb->TaskState) {                               //根据该任务的任务状态处理
                case OS_TASK_STATE_RDY:                               //如果任务状态均是与时间事件无关，就无需理会
                case OS_TASK_STATE_PEND:
                case OS_TASK_STATE_SUSPENDED:
                case OS_TASK_STATE_PEND_SUSPENDED:
                     break;

                case OS_TASK_STATE_DLY:                               //如果是延时状态
                     p_tcb->TickRemain = p_tcb->TickCtrMatch          //计算延时的的剩余时间 
                                       - OSTickCtr;
                     if (OSTickCtr == p_tcb->TickCtrMatch) {          //如果任务期满 
                         p_tcb->TaskState = OS_TASK_STATE_RDY;        //修改任务状态量为就绪状态
                         OS_TaskRdy(p_tcb);                           //让任务就绪
                     } else {                                         //如果任务未期满（由于升序排列，该列表后面的任务肯定也未期满）
                         done             = DEF_TRUE;                 //不再遍历该列表，退出 while 循环
                     }
                     break;

                case OS_TASK_STATE_PEND_TIMEOUT:                      //如果是有期限等待状态
                     p_tcb->TickRemain = p_tcb->TickCtrMatch          //计算期限的的剩余时间
                                       - OSTickCtr;
                     if (OSTickCtr == p_tcb->TickCtrMatch) {          //如果任务期满 
#if (OS_MSG_EN > 0u)                                                  //如果使能了消息队列（普通消息队列或任务消息队列）
                         p_tcb->MsgPtr     = (void      *)0;          //把任务保存接收到消息的地址的成员清空
                         p_tcb->MsgSize    = (OS_MSG_SIZE)0u;         //把任务保存接收到消息的长度的成员清零
#endif
                         p_tcb->TS         = OS_TS_GET();             //记录任务结束等待的时间戳
                         OS_PendListRemove(p_tcb);                    //从等待列表移除该任务
                         OS_TaskRdy(p_tcb);                           //让任务就绪
                         p_tcb->TaskState  = OS_TASK_STATE_RDY;       //修改任务状态量为就绪状态
                         p_tcb->PendStatus = OS_STATUS_PEND_TIMEOUT;  //记录等待状态为超时
                         p_tcb->PendOn     = OS_TASK_PEND_ON_NOTHING; //记录等待内核对象变量为空
                     } else {                                         //如果任务未期满（由于升序排列，该列表后面的任务肯定也未期满）
                         done              = DEF_TRUE;                //不再遍历该列表，退出 while 循环
                     }
                     break;

                case OS_TASK_STATE_DLY_SUSPENDED:                     //如果是延时中被挂起状态
                     p_tcb->TickRemain = p_tcb->TickCtrMatch          //计算延时的的剩余时间 
                                       - OSTickCtr;
                     if (OSTickCtr == p_tcb->TickCtrMatch) {          //如果任务期满
                         p_tcb->TaskState  = OS_TASK_STATE_SUSPENDED; //修改任务状态量为被挂起状态
                         OS_TickListRemove(p_tcb);                    //从节拍列表移除该任务
                     } else {                                         //如果任务未期满（由于升序排列，该列表后面的任务肯定也未期满）
                         done              = DEF_TRUE;                //不再遍历该列表，退出 while 循环
                     }
                     break;

                case OS_TASK_STATE_PEND_TIMEOUT_SUSPENDED:            //如果是有期限等待中被挂起状态
                     p_tcb->TickRemain = p_tcb->TickCtrMatch          //计算期限的的剩余时间
                                       - OSTickCtr;
                     if (OSTickCtr == p_tcb->TickCtrMatch) {          //如果任务期满 
#if (OS_MSG_EN > 0u)                                                  //如果使能了消息队列（普通消息队列或任务消息队列）
                         p_tcb->MsgPtr     = (void      *)0;          //把任务保存接收到消息的地址的成员清空
                         p_tcb->MsgSize    = (OS_MSG_SIZE)0u;         //把任务保存接收到消息的长度的成员清零
#endif
                         p_tcb->TS         = OS_TS_GET();             //记录任务结束等待的时间戳
                         OS_PendListRemove(p_tcb);                    //从等待列表移除该任务
                         OS_TickListRemove(p_tcb);                    //从节拍列表移除该任务
                         p_tcb->TaskState  = OS_TASK_STATE_SUSPENDED; //修改任务状态量为被挂起状态
                         p_tcb->PendStatus = OS_STATUS_PEND_TIMEOUT;  //记录等待状态为超时
                         p_tcb->PendOn     = OS_TASK_PEND_ON_NOTHING; //记录等待内核对象变量为空
                     } else {                                         //如果任务未期满（由于升序排列，该列表后面的任务肯定也未期满）
                         done              = DEF_TRUE;                //不再遍历该列表，退出 while 循环
                     }
                     break;

                default:
                     break;
            }
            p_tcb = p_tcb_next;                                       //遍历节拍列表的下一个任务
        } else {                                                      //如果该任务为空（节拍列表后面肯定也都是空的）
            done  = DEF_TRUE;                                         //不再遍历该列表，退出 while 循环
        }
    }
    ts_end = OS_TS_GET() - ts_start;                                  //获取 OS_TickTask() 任务的结束时间戳，并计算其执行时间
    if (OSTickTaskTimeMax < ts_end) {                                 //更新 OS_TickTask() 任务的最大运行时间
        OSTickTaskTimeMax = ts_end;
    }
    OS_CRITICAL_EXIT();                                               //退出临界段
}






















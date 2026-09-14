#include <stdio.h>
#include <windows.h>
#include <string.h>

#define LOOP 1000000   // 각 프로세스가 임계구역에 진입하는 횟수

typedef struct {
    int pid;
    enum { Ready, Running, Waiting, Done } state;
    int pc;
    int schedule;
    int addr;
} Process;

typedef enum { NO_SYNC, PETERSON, TEST_AND_SET } Sync_Mode;

typedef struct {
    Process* volatile p;        // 현재 임계구역 안에 있는 프로세스
    volatile int shared_data;   // 임계구역이 보호하는 공유 데이터
    volatile LONG inside;       // 임계구역 안에 동시에 있는 프로세스 수
    volatile LONG violation;    // 상호배제가 깨진 횟수
    Sync_Mode mode;

    volatile int flag[2];       // Peterson: 들어가고 싶다는 의사 표시
    volatile int turn;          // Peterson: 양보한 차례

    volatile LONG lock;         // Test-and-Set: 0 = 열림, 1 = 잠김
} Critical_Section;

typedef struct {
    Process* p;
    Critical_Section* cs;
    HANDLE start;
} Thread_Arg;

Process Create_Process(int pid, char* state, int pc, int schedule, int addr) {
    Process p;
    p.pid = pid;
    p.state = Ready;
    p.pc = pc;
    p.schedule = schedule;
    p.addr = addr;

    return p;
}

Critical_Section Init_Section(Sync_Mode mode) {
    Critical_Section cs;
    memset(&cs, 0, sizeof(cs));
    cs.p = NULL;
    cs.mode = mode;
    return cs;
}

// 진입 구역 (entry section)
void Enter_Cs(Critical_Section* cs, Process* p) {
    int i = p->pid - 1;
    int j = 1 - i;

    p->state = Waiting;
    if (cs->mode == PETERSON) {
        cs->flag[i] = 1;                            // 나 들어가고 싶어
        cs->turn = j;                               // 그래도 너 먼저
        MemoryBarrier();                            // CPU가 위 store와 아래 load 순서를 바꾸지 못하게 막음
        while (cs->flag[j] && cs->turn == j)        // 상대가 원하고 상대 차례면 바쁜 대기
            YieldProcessor();
    } else if (cs->mode == TEST_AND_SET) {
        while (InterlockedExchange(&cs->lock, 1) == 1)  // 원자적으로 1을 쓰고 이전 값을 확인
            YieldProcessor();
    }
    p->state = Running;

    if (InterlockedIncrement(&cs->inside) > 1)      // 이미 누가 안에 있으면 상호배제 위반
        InterlockedIncrement(&cs->violation);
    cs->p = p;
}

// 퇴출 구역 (exit section)
void Exit_Cs(Critical_Section* cs, Process* p) {
    int i = p->pid - 1;

    cs->p = NULL;
    InterlockedDecrement(&cs->inside);
    if (cs->mode == PETERSON)
        cs->flag[i] = 0;
    else if (cs->mode == TEST_AND_SET)
        InterlockedExchange(&cs->lock, 0);
    p->state = Ready;
}

DWORD WINAPI Run_Process(LPVOID arg) {
    Thread_Arg* t = (Thread_Arg*)arg;
    WaitForSingleObject(t->start, INFINITE);        // 두 프로세스가 동시에 출발하도록 대기

    for (int k = 0; k < LOOP; k++) {
        Enter_Cs(t->cs, t->p);

        // ---- 임계구역 (critical section) ----
        int tmp = t->cs->shared_data;               // 읽고
        tmp = tmp + 1;                              // 수정하고
        t->cs->shared_data = tmp;                   // 쓴다 -> 이 사이에 끼어들면 갱신이 사라짐
        // -------------------------------------

        Exit_Cs(t->cs, t->p);
        // 나머지 구역 (remainder section)
    }
    t->p->state = Done;
    return 0;
}

void show_cs(Critical_Section cs) {
    if (cs.p == NULL) {
        printf("Critical Section is empty\n");
        return;
    }
    printf("Pid : %d \nstate : %d\npc : %d \nschedule : %d\nAddr : %d\n", cs.p->pid, cs.p->state, cs.p->pc, cs.p->schedule, cs.p->addr);
}

void Run_Test(Sync_Mode mode, const char* name) {
    Process p1 = Create_Process(1, 0, 1001, 0, 100);
    Process p2 = Create_Process(2, 0, 1002, 1, 101);
    Critical_Section cs = Init_Section(mode);
    HANDLE start = CreateEvent(NULL, TRUE, FALSE, NULL);
    Thread_Arg a1 = { &p1, &cs, start };
    Thread_Arg a2 = { &p2, &cs, start };
    HANDLE th[2];

    th[0] = CreateThread(NULL, 0, Run_Process, &a1, 0, NULL);
    th[1] = CreateThread(NULL, 0, Run_Process, &a2, 0, NULL);

    printf("\n[%s]\n", name);
    ULONGLONG begin = GetTickCount64();
    SetEvent(start);
    WaitForMultipleObjects(2, th, TRUE, INFINITE);
    unsigned long elapsed = (unsigned long)(GetTickCount64() - begin);

    printf("expected  : %d\n", 2 * LOOP);
    printf("result    : %d (lost %d)\n", cs.shared_data, 2 * LOOP - cs.shared_data);
    printf("violation : %ld\n", cs.violation);
    printf("time      : %lu ms\n", elapsed);
    printf("P1 %d, P2 %d / ", p1.state, p2.state);
    show_cs(cs);

    CloseHandle(th[0]);
    CloseHandle(th[1]);
    CloseHandle(start);
}

int main() {
    printf("Start Critical Section Problem... (%d loops per process)\n", LOOP);
    Run_Test(NO_SYNC, "1. No synchronization");
    Run_Test(PETERSON, "2. Peterson's algorithm (software)");
    Run_Test(TEST_AND_SET, "3. Test-and-Set (hardware atomic instruction)");
    return 0;
}

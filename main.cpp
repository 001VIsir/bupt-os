#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <windows.h>
#include <process.h>

// 定义最大进程数和内存大小
#define MAX_PROCESSES 100
#define MEMORY_SIZE 1024
#define DEFAULT_TIME_SLICE 5
#define MAX_FILENAME 32
#define FILE_MAX_PATH 256  // 修改为FILE_MAX_PATH以避免与Windows MAX_PATH冲突
#define DISK_SIZE 2048
#define BLOCK_SIZE 16
#define MAX_FILE_BLOCKS 128
#define MAX_FILES 64

// 进程状态枚举
typedef enum {
    READY,
    RUNNING,
    BLOCKED,
    TERMINATED
} ProcessState;

// 新增：调度算法枚举
typedef enum {
    FCFS,       // 先来先服务
    PRIORITY,   // 优先级调度
    RR          // 时间片轮转
} ScheduleAlgorithm;

// 进程控制块
typedef struct PCB {
    int pid;                // 进程ID
    char name[20];          // 进程名称
    ProcessState state;     // 进程状态
    int priority;           // 优先级，数值越小优先级越高
    int time_slice;         // 时间片
    int memory_start;       // 内存起始地址
    int memory_size;        // 内存大小
    struct PCB *next;       // 链表指针
} PCB;

// 内存块
typedef struct MemoryBlock {
    int start_address;       // 内存块起始地址
    int size;                // 内存块大小
    bool is_allocated;       // 是否已分配
    int pid;                 // 分配给的进程ID
    struct MemoryBlock *next;// 链表指针
} MemoryBlock;

// 中断类型
typedef enum {
    TIMER_INTERRUPT,
    IO_INTERRUPT,
    SYSTEM_CALL
} InterruptType;

// 中断结构
typedef struct Interrupt {
    InterruptType type;     // 中断类型
    int source;             // 中断源
    int priority;           // 中断优先级
} Interrupt;

// 文件类型枚举
typedef enum {
    FILE_TYPE,
    DIRECTORY_TYPE
} FileType;

// 文件控制块
typedef struct FCB {
    char name[MAX_FILENAME];    // 文件名
    FileType type;              // 文件类型（文件/目录）
    int size;                   // 文件大小
    int block_count;            // 占用块数
    int first_block;            // 第一个数据块索引
    time_t create_time;         // 创建时间
    time_t modify_time;         // 修改时间
    struct FCB *parent;         // 父目录
    struct FCB *child;          // 子文件/目录（如果是目录）
    struct FCB *sibling;        // 同级文件/目录
} FCB;

// 磁盘块结构
typedef struct {
    bool is_allocated;          // 是否已分配
    int next_block;             // 下一块索引（用于文件块链）
} DiskBlock;

// 全局变量
PCB *ready_queue = NULL;        // 就绪队列
PCB *running_process = NULL;    // 当前运行进程
PCB *blocked_queue = NULL;      // 阻塞队列
MemoryBlock *memory = NULL;     // 内存链表
DiskBlock disk[DISK_SIZE/BLOCK_SIZE]; // 磁盘块数组
FCB *root_directory = NULL;           // 根目录
FCB *current_directory = NULL;        // 当前目录
int next_pid = 1;               // 下一个可用的进程ID
int time_counter = 0;           // 时钟计数器
int system_running = 1;         // 系统运行状态
ScheduleAlgorithm current_algorithm = RR;  // 当前调度算法，默认为时间片轮转
bool auto_run = false;        // 是否自动运行时间片
HANDLE timer_thread = NULL;   // 定时器线程句柄
bool timer_running = false;   // 定时器线程运行状态

// 中断相关全局变量
bool system_interrupt_flag = false;     // 中断标志
PCB* interrupted_process = NULL;        // 被中断的进程

// 函数声明
void init_system();
void display_help();
void process_command(char *command);
PCB* create_process(char *name, int memory_size, int priority, int time_slice);
void terminate_process(int pid);
void block_process(int pid);
void wakeup_process(int pid);
void schedule_process();
int allocate_memory(int size, int pid);
void free_memory(int pid);
void display_memory();
void display_processes();
void handle_timer_interrupt();
void run_simulation();
void cleanup_system();
void add_to_ready_queue(PCB *proc);
PCB* remove_from_ready_queue();
void add_to_blocked_queue(PCB *proc);
PCB* remove_from_blocked_queue(int pid);
void set_schedule_algorithm(ScheduleAlgorithm algorithm);
const char* get_algorithm_name(ScheduleAlgorithm algorithm);
void toggle_auto_run();
unsigned __stdcall timer_thread_func(void* arg);  // 定时器线程函数
void init_file_system();
void display_file_help();
FCB* create_file(const char* name, FileType type, FCB* parent);
void delete_file(FCB* file);
int allocate_disk_block(int count);
void free_disk_block(int first_block);
void list_directory(FCB* dir);
FCB* find_file(FCB* dir, const char* name);
void display_file_system();
FCB* change_directory(const char* path);
void create_file_command(const char* name, int size);
void create_directory_command(const char* name);
void delete_file_command(const char* name);
void delete_directory_command(const char* name);
void list_command();
void change_directory_command(const char* path);
void display_disk();
// 中断处理相关函数声明
void handle_system_interrupt();
void resume_interrupted_process();

// 初始化系统
void init_system() {
    // 初始化内存：一个大内存块
    memory = (MemoryBlock*)malloc(sizeof(MemoryBlock));
    memory->start_address = 0;
    memory->size = MEMORY_SIZE;
    memory->is_allocated = false;
    memory->pid = -1;
    memory->next = NULL;

    // 初始化随机数种子
    srand(time(NULL));

    // 初始化文件系统
    init_file_system();

    printf("系统初始化完成，内存大小: %d，磁盘大小: %d\n", MEMORY_SIZE, DISK_SIZE);
}

// 初始化文件系统
void init_file_system() {
    // 初始化磁盘块
    for (int i = 0; i < DISK_SIZE/BLOCK_SIZE; i++) {
        disk[i].is_allocated = false;
        disk[i].next_block = -1;
    }

    // 创建根目录
    root_directory = (FCB*)malloc(sizeof(FCB));
    strcpy(root_directory->name, "/");
    root_directory->type = DIRECTORY_TYPE;
    root_directory->size = 0;
    root_directory->block_count = 0;
    root_directory->first_block = -1;
    root_directory->create_time = time(NULL);
    root_directory->modify_time = time(NULL);
    root_directory->parent = root_directory; // 根目录的父目录是它自己
    root_directory->child = NULL;
    root_directory->sibling = NULL;

    // 设置当前目录为根目录
    current_directory = root_directory;
}

// 显示文件系统帮助
void display_file_help() {
    printf("\n===== 文件系统命令帮助 =====\n");
    printf("ls                  - 列出当前目录的文件\n");
    printf("mkdir <dirname>     - 创建新目录\n");
    printf("rmdir <dirname>     - 删除目录\n");
    printf("touch <filename> <size> - 创建新文件\n");
    printf("rm <filename>       - 删除文件\n");
    printf("cd <path>           - 切换目录\n");
    printf("pwd                 - 显示当前目录路径\n");
    printf("diskstat            - 显示磁盘使用情况\n");
    printf("===============================\n\n");
}

// 显示帮助信息 - 增��文件系统命令
void display_help() {
    printf("\n===== 操作系统模拟程序帮助 =====\n");
    printf("help                - 显示此帮助信息\n");
    printf("ps                  - 显示所有进程\n");
    printf("new <name> <size> <priority> [time_slice] - 创建新进程\n");
    printf("kill <pid>          - 终止进程\n");
    printf("block <pid>         - 阻塞进程\n");
    printf("wakeup <pid>        - 唤醒进程\n");
    printf("memshow             - 显示内存使用情况\n");
    printf("run                 - 运行模拟系统一个时间片\n");
    printf("auto                - 切换自动/手动运行模式\n");
    printf("schedule            - 显示当前调度算法\n");
    printf("schedule fcfs       - 设置调度算法为先来先服务\n");
    printf("schedule priority   - 设置调度���法为优先级调度\n");
    printf("schedule rr         - 设置调度算法为时间片轮转\n");
    printf("filehelp            - 显示文件系统命令帮助\n");
    
    // 新增中断相关命令帮助
    printf("stop                - 中断当前运行的进程\n");
    printf("recover             - 恢复被中断的进程\n");
    printf("intstat             - 显示中断系统状态\n");
    
    printf("exit                - 退出模拟器\n");
    printf("================================\n\n");
}

// 处理命令 - 增加文件系统命令
void process_command(char *command) {
    char cmd[20] = {0};
    char arg1[20] = {0};
    char arg2[20] = {0};
    char arg3[20] = {0};
    char arg4[20] = {0};

    sscanf(command, "%s %s %s %s %s", cmd, arg1, arg2, arg3, arg4);

    if (strcmp(cmd, "help") == 0) {
        display_help();
    }
    else if (strcmp(cmd, "ps") == 0) {
        display_processes();
    }
    else if (strcmp(cmd, "new") == 0) {
        if (arg1[0] != '\0' && arg2[0] != '\0' && arg3[0] != '\0') {
            int size = atoi(arg2);
            int priority = atoi(arg3);
            int time_slice = arg4[0] != '\0' ? atoi(arg4) : DEFAULT_TIME_SLICE;
            
            PCB *new_proc = create_process(arg1, size, priority, time_slice);
            if (new_proc) {
                printf("进程创建成功，PID: %d\n", new_proc->pid);
            }
        } else {
            printf("用法: new <name> <size> <priority> [time_slice]\n");
        }
    }
    else if (strcmp(cmd, "kill") == 0) {
        if (arg1[0] != '\0') {
            terminate_process(atoi(arg1));
        } else {
            printf("用法: kill <pid>\n");
        }
    }
    else if (strcmp(cmd, "block") == 0) {
        if (arg1[0] != '\0') {
            block_process(atoi(arg1));
        } else {
            printf("用法: block <pid>\n");
        }
    }
    else if (strcmp(cmd, "wakeup") == 0) {
        if (arg1[0] != '\0') {
            wakeup_process(atoi(arg1));
        } else {
            printf("用法: wakeup <pid>\n");
        }
    }
    else if (strcmp(cmd, "memshow") == 0) {
        display_memory();
    }
    else if (strcmp(cmd, "run") == 0) {
        run_simulation();
    }
    else if (strcmp(cmd, "auto") == 0) {
        toggle_auto_run();
    }
    else if (strcmp(cmd, "schedule") == 0) {
        if (arg1[0] == '\0') {
            printf("当前调度算法: %s\n", get_algorithm_name(current_algorithm));
        } else if (strcmp(arg1, "fcfs") == 0) {
            set_schedule_algorithm(FCFS);
        } else if (strcmp(arg1, "priority") == 0) {
            set_schedule_algorithm(PRIORITY);
        } else if (strcmp(arg1, "rr") == 0) {
            set_schedule_algorithm(RR);
        } else {
            printf("未知的调度算法: %s\n", arg1);
            printf("可用的调度算法: fcfs, priority, rr\n");
        }
    }
    else if (strcmp(cmd, "filehelp") == 0) {
        display_file_help();
    }
    else if (strcmp(cmd, "ls") == 0) {
        list_command();
    }
    else if (strcmp(cmd, "mkdir") == 0) {
        if (arg1[0] != '\0') {
            create_directory_command(arg1);
        } else {
            printf("用法: mkdir <dirname>\n");
        }
    }
    else if (strcmp(cmd, "rmdir") == 0) {
        if (arg1[0] != '\0') {
            delete_directory_command(arg1);
        } else {
            printf("用法: rmdir <dirname>\n");
        }
    }
    else if (strcmp(cmd, "touch") == 0) {
        if (arg1[0] != '\0' && arg2[0] != '\0') {
            create_file_command(arg1, atoi(arg2));
        } else {
            printf("用法: touch <filename> <size>\n");
        }
    }
    else if (strcmp(cmd, "rm") == 0) {
        if (arg1[0] != '\0') {
            delete_file_command(arg1);
        } else {
            printf("用法: rm <filename>\n");
        }
    }
    else if (strcmp(cmd, "cd") == 0) {
        change_directory_command(arg1);
    }
    else if (strcmp(cmd, "pwd") == 0) {
        char path[FILE_MAX_PATH] = {0};  // 使用FILE_MAX_PATH替代MAX_PATH
        FCB* temp = current_directory;
        FCB* path_stack[FILE_MAX_PATH/2];  // 使用FILE_MAX_PATH替代MAX_PATH
        int stack_size = 0;
        
        // 生成从当前目录到根目录的路径
        while (temp != root_directory) {
            path_stack[stack_size++] = temp;
            temp = temp->parent;
        }
        
        // 打印路径
        strcpy(path, "/");
        for (int i = stack_size - 1; i >= 0; i--) {
            strcat(path, path_stack[i]->name);
            if (i > 0) strcat(path, "/");
        }
        
        printf("当前目录: %s\n", path);
    }
    else if (strcmp(cmd, "diskstat") == 0) {
        display_disk();
    }
    else if (strcmp(cmd, "exit") == 0) {
        system_running = false;
        auto_run = false;  // 确保退出前关闭自动运行
    }
    else if (strcmp(cmd, "stop") == 0) {
        if (running_process == NULL) {
            printf("当前没有运行的进程可以中断\n");
        } else if (system_interrupt_flag) {
            printf("系统已经处于中断状态\n");
        } else {
            printf("\n执行系统中断命令\n");
            handle_system_interrupt();
        }
    }
    else if (strcmp(cmd, "recover") == 0) {
        if (!system_interrupt_flag || interrupted_process == NULL) {
            printf("没有被中断的进程需要恢复\n");
        } else {
            printf("\n执行恢复被中断进程命令\n");
            resume_interrupted_process();
        }
    }
    else if (strcmp(cmd, "intstat") == 0) {
        printf("\n===== 中断状态 =====\n");
        printf("系统中断状态: %s\n", system_interrupt_flag ? "活动" : "非活动");
        if (interrupted_process != NULL) {
            printf("被中断进程: %s (PID=%d)\n", 
                   interrupted_process->name, interrupted_process->pid);
        } else {
            printf("被中断进程: 无\n");
        }
        printf("====================\n\n");
    }
    else {
        printf("未知命令。输入 'help' 查看可用命令。\n");
    }
}

// 创建进程 - 修改为接受time_slice参数
PCB* create_process(char *name, int memory_size, int priority, int time_slice) {
    // 检查进程名是否为空
    if (name == NULL || strlen(name) == 0) {
        printf("错误: 进程名不能为空\n");
        return NULL;
    }

    // 检查内存大小是否有效
    if (memory_size <= 0 || memory_size > MEMORY_SIZE) {
        printf("错误: 内存大小无效\n");
        return NULL;
    }

    // 检查时间片是否有效
    if (time_slice <= 0) {
        printf("错误: 时间片必须大于0\n");
        return NULL;
    }

    // 分配内存
    int mem_start = allocate_memory(memory_size, next_pid);
    if (mem_start == -1) {
        printf("错误: 无足够内存可分配\n");
        return NULL;
    }

    // 创建PCB
    PCB *new_process = (PCB*)malloc(sizeof(PCB));
    new_process->pid = next_pid++;
    strncpy(new_process->name, name, sizeof(new_process->name) - 1);
    new_process->name[sizeof(new_process->name) - 1] = '\0';
    new_process->state = READY;
    new_process->priority = priority;
    new_process->time_slice = time_slice;  // 使用传入的时间片
    new_process->memory_start = mem_start;
    new_process->memory_size = memory_size;
    new_process->next = NULL;

    // 添加到就绪队列
    add_to_ready_queue(new_process);

    printf("进程 %s (PID=%d) 已创建，内存分配: 起始=%d, 大小=%d, 时间片=%d\n",
           name, new_process->pid, mem_start, memory_size, time_slice);

    return new_process;
}

// 终止进程
void terminate_process(int pid) {
    // 检查正在运行的进程
    if (running_process && running_process->pid == pid) {
        printf("终止运行中的进程 %s (PID=%d)\n", running_process->name, running_process->pid);
        free_memory(pid);
        free(running_process);
        running_process = NULL;
        schedule_process(); // 重新调度
        return;
    }

    // 检查就绪队列
    if (ready_queue != NULL) {
        PCB *prev = NULL;
        PCB *current = ready_queue;

        while (current != NULL) {
            if (current->pid == pid) {
                // 找到要终止的进程
                if (prev == NULL) {
                    ready_queue = current->next; // 当前是头节点
                } else {
                    prev->next = current->next; // 更新链表
                }

                printf("终止就绪队列中的进程 %s (PID=%d)\n", current->name, current->pid);
                free_memory(pid);
                free(current);
                return;
            }

            prev = current;
            current = current->next;
        }
    }

    // 检查阻塞队列
    if (blocked_queue != NULL) {
        PCB *prev = NULL;
        PCB *current = blocked_queue;

        while (current != NULL) {
            if (current->pid == pid) {
                // 找到要终止的进程
                if (prev == NULL) {
                    blocked_queue = current->next; // 当前是头节点
                } else {
                    prev->next = current->next; // 更新链表
                }

                printf("终止阻塞队列中的进程 %s (PID=%d)\n", current->name, current->pid);
                free_memory(pid);
                free(current);
                return;
            }

            prev = current;
            current = current->next;
        }
    }

    printf("未找到PID=%d的进程\n", pid);
}

// 阻塞进程
void block_process(int pid) {
    // 只能阻塞当前运行的进程
    if (running_process && running_process->pid == pid) {
        printf("阻塞进程 %s (PID=%d)\n", running_process->name, running_process->pid);
        running_process->state = BLOCKED;
        add_to_blocked_queue(running_process);
        running_process = NULL;
        schedule_process(); // 重新调度
    } else {
        printf("只能阻塞当前运行的进程，PID=%d不是当前运行的进程\n", pid);
    }
}

// 唤醒进程
void wakeup_process(int pid) {
    PCB *proc = remove_from_blocked_queue(pid);
    if (proc != NULL) {
        printf("唤醒进程 %s (PID=%d)\n", proc->name, proc->pid);
        proc->state = READY;
        add_to_ready_queue(proc);
    } else {
        printf("未找到阻塞队列中PID=%d的进程\n", pid);
    }
}

// 进程调度
void schedule_process() {
    // 如果有正在运行的进程，先检查是否时间片用完
    if (running_process != NULL) {
        if (running_process->time_slice > 0) {
            // 时间片未用完，继续运行
            return;
        }

        // 时间片用完，放回就绪队列
        printf("进程 %s (PID=%d) 时间片用完，切换...\n", running_process->name, running_process->pid);
        running_process->time_slice = DEFAULT_TIME_SLICE; // 重置时间片
        running_process->state = READY;
        add_to_ready_queue(running_process);
        running_process = NULL;
    }

    // 从就绪队列选择下一个进程
    PCB *next_process = remove_from_ready_queue();
    
    // 如果找到了下一个要运行的进程
    if (next_process != NULL) {
        running_process = next_process;
        running_process->state = RUNNING;
        printf("调度进程 %s (PID=%d) 开始运行\n", running_process->name, running_process->pid);
    } else {
        printf("就绪队列为空，没有可运行的进程\n");
        
        // 如果没有其他进程可运行，且有被中断的进程，考虑自动恢复
        if (system_interrupt_flag && interrupted_process != NULL) {
            printf("就绪队列为空，系统自动恢复被中断的进程\n");
            resume_interrupted_process();
        }
    }
}

// 内存分配（使用最佳适应算法）
int allocate_memory(int size, int pid) {
    MemoryBlock *current = memory;
    MemoryBlock *best_fit = NULL;
    int best_size = MEMORY_SIZE + 1; // 初始化为一个很大的值

    // 查找最合适的内存块（最小合适)
    while (current != NULL) {
        if (!current->is_allocated && current->size >= size && current->size < best_size) {
            best_fit = current;
            best_size = current->size;
        }
        current = current->next;
    }

    // 没有找到合适的内存块
    if (best_fit == NULL) {
        return -1;
    }

    // 找到合适的内存块，进行分配
    int start_address = best_fit->start_address;

    if (best_fit->size > size) {
        // 分割内存块
        MemoryBlock *new_block = (MemoryBlock*)malloc(sizeof(MemoryBlock));
        new_block->start_address = start_address + size;
        new_block->size = best_fit->size - size;
        new_block->is_allocated = false;
        new_block->pid = -1;
        new_block->next = best_fit->next;

        best_fit->size = size;
        best_fit->next = new_block;
    }

    // 分配这个内存块
    best_fit->is_allocated = true;
    best_fit->pid = pid;

    return start_address;
}

// 释放内存
void free_memory(int pid) {
    printf("尝试释放进程 PID=%d 的内存资源\n", pid);
    
    // 第一遍：标记所有要释放的内存块
    MemoryBlock *current = memory;
    int freed_blocks = 0;
    int total_freed_size = 0;
    
    while (current != NULL) {
        if (current->is_allocated && current->pid == pid) {
            printf("释放内存: 地址=%d, 大小=%d\n", current->start_address, current->size);
            current->is_allocated = false;
            current->pid = -1;
            freed_blocks++;
            total_freed_size += current->size;
        }
        current = current->next;
    }
    
    if (freed_blocks == 0) {
        printf("没有找到PID=%d的内存块\n", pid);
        return;
    }
    
    // 第二遍：合并相邻的空闲内存块（循环直到无法继续合并）
    bool merged;
    do {
        merged = false;
        current = memory;
        
        while (current != NULL && current->next != NULL) {
            if (!current->is_allocated && !current->next->is_allocated) {
                // 合并当前块和下一个块
                MemoryBlock *next_block = current->next;
                printf("合并内存块: 地址=%d+%d, 大小=%d+%d\n", 
                       current->start_address, current->size,
                       next_block->start_address, next_block->size);
                
                current->size += next_block->size;
                current->next = next_block->next;
                
                // 释放被合并的块
                free(next_block);
                merged = true;
                
                // 不移动当前指针，继续检查是否可以与新的next块合并
                continue;
            }
            
            // 移动到下一块
            current = current->next;
        }
    } while (merged);
    
    printf("内存释放完成: 共释放%d个块，总大小%d\n", freed_blocks, total_freed_size);
    
    // 打印当前内存状态，帮助调试
    printf("内存块状态检查:\n");
    current = memory;
    while (current != NULL) {
        printf("  地址=%d, 大小=%d, 状态=%s, PID=%d\n",
               current->start_address, current->size,
               current->is_allocated ? "已分配" : "空闲",
               current->pid);
        current = current->next;
    }
}

// 创建文件节点
FCB* create_file(const char* name, FileType type, FCB* parent) {
    // 检查同名文件是否已存在
    FCB* existing = find_file(parent, name);
    if (existing != NULL) {
        printf("错误: %s '%s' 已存在\n", 
               (type == FILE_TYPE) ? "文件" : "目录", name);
        return NULL;
    }
    
    // 创建文件控制块
    FCB* file = (FCB*)malloc(sizeof(FCB));
    if (file == NULL) {
        printf("错误: 内存不足，无法创建%s\n", 
               (type == FILE_TYPE) ? "文件" : "目录");
        return NULL;
    }
    
    strncpy(file->name, name, MAX_FILENAME-1);
    file->name[MAX_FILENAME-1] = '\0';
    file->type = type;
    file->size = 0;
    file->block_count = 0;
    file->first_block = -1;
    file->create_time = time(NULL);
    file->modify_time = time(NULL);
    file->parent = parent;
    file->child = NULL;
    file->sibling = NULL;
    
    // 添加到父目录的子列表
    if (parent->child == NULL) {
        parent->child = file;
    } else {
        FCB* sibling = parent->child;
        while (sibling->sibling != NULL) {
            sibling = sibling->sibling;
        }
        sibling->sibling = file;
    }
    
    parent->modify_time = time(NULL);
    return file;
}

// 分配磁盘块
int allocate_disk_block(int count) {
    if (count <= 0) return -1;
    
    int first_block = -1;
    int last_block = -1;
    int blocks_allocated = 0;
    
    // 寻找空闲块并分配
    for (int i = 0; i < DISK_SIZE/BLOCK_SIZE && blocks_allocated < count; i++) {
        if (!disk[i].is_allocated) {
            disk[i].is_allocated = true;
            disk[i].next_block = -1;
            
            if (first_block == -1) {
                first_block = i;
                last_block = i;
            } else {
                disk[last_block].next_block = i;
                last_block = i;
            }
            
            blocks_allocated++;
        }
    }
    
    // 如果无法满足请求数量，释放已分配的块
    if (blocks_allocated < count) {
        if (first_block != -1) {
            free_disk_block(first_block);
        }
        return -1;
    }
    
    return first_block;
}

// 释放磁盘块
void free_disk_block(int first_block) {
    if (first_block < 0 || first_block >= DISK_SIZE/BLOCK_SIZE) return;
    
    int current = first_block;
    int next;
    
    while (current != -1) {
        if (current >= 0 && current < DISK_SIZE/BLOCK_SIZE) {
            next = disk[current].next_block;
            disk[current].is_allocated = false;
            disk[current].next_block = -1;
            current = next;
        } else {
            break;
        }
    }
}

// 递归删除文件/目录
void delete_file(FCB* file) {
    if (file == NULL) return;
    
    // 如果是目录，先删除所有子文件/目录
    if (file->type == DIRECTORY_TYPE) {
        FCB* child = file->child;
        while (child != NULL) {
            FCB* next_child = child->sibling;
            delete_file(child);
            child = next_child;
        }
    }
    
    // 释放文件��用的磁盘块
    if (file->first_block != -1) {
        free_disk_block(file->first_block);
    }
    
    // 从父目录中移除
    FCB* parent = file->parent;
    if (parent != NULL) {
        if (parent->child == file) {
            parent->child = file->sibling;
        } else {
            FCB* sibling = parent->child;
            while (sibling != NULL && sibling->sibling != file) {
                sibling = sibling->sibling;
            }
            if (sibling != NULL) {
                sibling->sibling = file->sibling;
            }
        }
        
        parent->modify_time = time(NULL);
    }
    
    // 释放FCB
    free(file);
}

// 查找文件/目录
FCB* find_file(FCB* dir, const char* name) {
    if (dir == NULL || name == NULL) return NULL;
    
    FCB* child = dir->child;
    while (child != NULL) {
        if (strcmp(child->name, name) == 0) {
            return child;
        }
        child = child->sibling;
    }
    
    return NULL;
}

// 列出目录内容
void list_directory(FCB* dir) {
    if (dir == NULL || dir->type != DIRECTORY_TYPE) {
        printf("错误: 不是有效的目录\n");
        return;
    }
    
    printf("\n目录 '%s' 的内容:\n", dir->name);
    printf("类型\t大小\t块数\t名称\t\t创建时间\n");
    printf("------------------------------------------------------\n");
    
    FCB* child = dir->child;
    while (child != NULL) {
        char time_str[26];
        ctime_s(time_str, sizeof(time_str), &child->create_time);
        time_str[24] = '\0'; // 移除换行符
        
        printf("%s\t%d\t%d\t%-16s\t%s\n",
               (child->type == FILE_TYPE) ? "文件" : "目录",
               child->size,
               child->block_count,
               child->name,
               time_str);
        
        child = child->sibling;
    }
    printf("\n");
}

// 根据路径切换目录
FCB* change_directory(const char* path) {
    if (path == NULL || strlen(path) == 0) {
        return current_directory;
    }
    
    FCB* target;
    
    // 如果以'/'开头，从根目录开始
    if (path[0] == '/') {
        target = root_directory;
        path++;
    } else {
        target = current_directory;
    }
    
    char temp_path[FILE_MAX_PATH];  // 使用FILE_MAX_PATH替代MAX_PATH
    strncpy(temp_path, path, sizeof(temp_path)-1);
    temp_path[sizeof(temp_path)-1] = '\0';
    
    char* token = strtok(temp_path, "/");
    while (token != NULL) {
        if (strcmp(token, ".") == 0) {
            // 当前目录，什么都不做
        } else if (strcmp(token, "..") == 0) {
            // 上级目录
            if (target != root_directory) {
                target = target->parent;
            }
        } else {
            FCB* next = find_file(target, token);
            if (next == NULL || next->type != DIRECTORY_TYPE) {
                printf("错误: 目录 '%s' 不存��\n", token);
                return NULL;
            }
            target = next;
        }
        token = strtok(NULL, "/");
    }
    
    return target;
}

// ======= 文件系统命令实现 =======

// 创建文件命令
void create_file_command(const char* name, int size) {
    if (size <= 0) {
        printf("错误: 文件大小必须大于零\n");
        return;
    }
    
    // 计算所需块数（向上取整）
    int blocks_needed = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    
    if (blocks_needed > MAX_FILE_BLOCKS) {
        printf("错误: 文件过大，超过最大允许大小\n");
        return;
    }
    
    // 创建文件节点
    FCB* file = create_file(name, FILE_TYPE, current_directory);
    if (file == NULL) {
        return;
    }
    
    // ���配磁盘块
    int first_block = allocate_disk_block(blocks_needed);
    if (first_block == -1) {
        printf("错误: 磁盘空间不足，无法分配%d个块\n", blocks_needed);
        delete_file(file);
        return;
    }
    
    file->size = size;
    file->block_count = blocks_needed;
    file->first_block = first_block;
    
    printf("文件 '%s' 已创建���大小: %d 字节，占用 %d 个磁盘块\n",
           name, size, blocks_needed);
}

// 创建目录命令
void create_directory_command(const char* name) {
    FCB* dir = create_file(name, DIRECTORY_TYPE, current_directory);
    if (dir != NULL) {
        printf("目录 '%s' 已创建\n", name);
    }
}

// 删除文件命令
void delete_file_command(const char* name) {
    FCB* file = find_file(current_directory, name);
    if (file == NULL) {
        printf("错误: 文件 '%s' 不存在\n", name);
        return;
    }
    
    if (file->type != FILE_TYPE) {
        printf("错误: '%s' 是一个目录，请使用 rmdir 命令\n", name);
        return;
    }
    
    delete_file(file);
    printf("文件 '%s' 已删除\n", name);
}

// 删除目录命令
void delete_directory_command(const char* name) {
    FCB* dir = find_file(current_directory, name);
    if (dir == NULL) {
        printf("错误: 目录 '%s' 不存在\n", name);
        return;
    }
    
    if (dir->type != DIRECTORY_TYPE) {
        printf("错误: '%s' 是一个文件，请使用 rm 命令\n", name);
        return;
    }
    
    if (dir->child != NULL) {
        printf("警告: 目录 '%s' 不为空，将删除其所有内容\n", name);
    }
    
    delete_file(dir);
    printf("目录 '%s' 已删除\n", name);
}

// 列出当前目录内容
void list_command() {
    list_directory(current_directory);
}

// 切换目录命令
void change_directory_command(const char* path) {
    if (path == NULL || strlen(path) == 0) {
        // 无参数时切换到根目录
        current_directory = root_directory;
        printf("已切换到根目录\n");
        return;
    }
    
    FCB* new_dir = change_directory(path);
    if (new_dir != NULL) {
        current_directory = new_dir;
        printf("已切换到目录 '%s'\n", path);
    }
}

// 显示磁盘使用情况
void display_disk() {
    int total_blocks = DISK_SIZE / BLOCK_SIZE;
    int used_blocks = 0;
    int free_blocks = 0;
    
    for (int i = 0; i < total_blocks; i++) {
        if (disk[i].is_allocated) {
            used_blocks++;
        } else {
            free_blocks++;
        }
    }
    
    printf("\n===== 磁盘使用情况 =====\n");
    printf("总块数: %d\n", total_blocks);
    printf("已用块数: %d (%.2f%%)\n", used_blocks, 100.0 * used_blocks / total_blocks);
    printf("空闲块数: %d (%.2f%%)\n", free_blocks, 100.0 * free_blocks / total_blocks);
    printf("块大小: %d 字节\n", BLOCK_SIZE);
    printf("总容量: %d 字节\n", DISK_SIZE);
    printf("已用容量: %d 字节\n", used_blocks * BLOCK_SIZE);
    printf("可用容量: %d 字节\n", free_blocks * BLOCK_SIZE);
    printf("==========================\n\n");
}

// 清理系统资源
void cleanup_system() {
    // 释放所有进程资源
    if (running_process != NULL) {
        free(running_process);
        running_process = NULL;
    }

    // 清理就绪队列
    while (ready_queue != NULL) {
        PCB *temp = ready_queue;
        ready_queue = ready_queue->next;
        free(temp);
    }

    // 清理阻塞队列
    while (blocked_queue != NULL) {
        PCB *temp = blocked_queue;
        blocked_queue = blocked_queue->next;
        free(temp);
    }

    // 清理内存链表
    while (memory != NULL) {
        MemoryBlock *temp = memory;
        memory = memory->next;
        free(temp);
    }

    // 清理文件系统
    if (root_directory != NULL) {
        delete_file(root_directory);
        root_directory = NULL;
        current_directory = NULL;
    }

    printf("系统资源已清理\n");
}

// 设置调度算法
void set_schedule_algorithm(ScheduleAlgorithm algorithm) {
    current_algorithm = algorithm;
    printf("调度算法已设置为: %s\n", get_algorithm_name(algorithm));
    
    // 重新组织就绪队列
    if (ready_queue != NULL) {
        PCB* temp_queue = NULL;
        
        // 先取出所有进程
        while (ready_queue != NULL) {
            PCB* proc = remove_from_ready_queue();
            proc->next = temp_queue;
            temp_queue = proc;
        }
        
        // 再按照新算法重新插入
        while (temp_queue != NULL) {
            PCB* proc = temp_queue;
            temp_queue = temp_queue->next;
            proc->next = NULL;
            add_to_ready_queue(proc);
        }
    }
}

// 获取调度算法名称
const char* get_algorithm_name(ScheduleAlgorithm algorithm) {
    switch (algorithm) {
        case FCFS: return "先来先服务 (FCFS)";
        case PRIORITY: return "优先级调度 (Priority)";
        case RR: return "时间片轮转 (Round Robin)";
        default: return "未知算法";
    }
}

// 添加进程到就绪队列（按优先级排序）
void add_to_ready_queue(PCB *proc) {
    if (proc == NULL) return;
    proc->next = NULL;
    
    // 如果就绪队列为空
    if (ready_queue == NULL) {
        ready_queue = proc;
        return;
    }
    
    switch (current_algorithm) {
        case FCFS: {
            // 先来先服务：添加到队列尾部
            PCB *current = ready_queue;
            while (current->next != NULL) {
                current = current->next;
            }
            current->next = proc;
            break;
        }
            
        case PRIORITY: {
            // 优先级调度：根据优先级插��（优先级低的数字大）
            if (proc->priority < ready_queue->priority) {
                // 如果新进程优先级高于队首
                proc->next = ready_queue;
                ready_queue = proc;
            } else {
                // 找到合适的位置插入
                PCB *current = ready_queue;
                PCB *prev = NULL;
                
                while (current != NULL && proc->priority >= current->priority) {
                    prev = current;
                    current = current->next;
                }
                
                proc->next = current;
                if (prev != NULL) {
                    prev->next = proc;
                }
            }
            break;
        }
            
        case RR:
        default: {
            // 时间片轮转：添加到队列尾部
            PCB *current = ready_queue;
            while (current->next != NULL) {
                current = current->next;
            }
            current->next = proc;
            break;
        }
    }
}

// 从就绪队列中移除并返回进程 - 根据调度算法调整
PCB* remove_from_ready_queue() {
    if (ready_queue == NULL) {
        return NULL;
    }
    
    // 不管哪种调度算法，都是从队首取出进程
    // 因为在add_to_ready_queue已经按照各自算法排序好了
    PCB *proc = ready_queue;
    ready_queue = ready_queue->next;
    proc->next = NULL;
    return proc;
}

// 添加进程到阻塞队列
void add_to_blocked_queue(PCB *proc) {
    if (proc == NULL) return;

    proc->next = blocked_queue;
    blocked_queue = proc;
}

// 从阻塞队列中移除并返回指定PID的进程
PCB* remove_from_blocked_queue(int pid) {
    if (blocked_queue == NULL) {
        return NULL;
    }

    // 如果头节点就是要找的进程
    if (blocked_queue->pid == pid) {
        PCB *proc = blocked_queue;
        blocked_queue = blocked_queue->next;
        proc->next = NULL;
        return proc;
    }

    // 在链表中查找
    PCB *current = blocked_queue;
    while (current->next != NULL && current->next->pid != pid) {
        current = current->next;
    }

    // 如果找到了
    if (current->next != NULL && current->next->pid == pid) {
        PCB *proc = current->next;
        current->next = proc->next;
        proc->next = NULL;
        return proc;
    }

    return NULL; // 未找到
}

// ��换自动运行状态
void toggle_auto_run() {
    auto_run = !auto_run;
    if (auto_run) {
        printf("自动运行已开启（每秒执行一个时间片）\n");
        // 创建定时器线程
        timer_running = true;
        timer_thread = (HANDLE)_beginthreadex(NULL, 0, timer_thread_func, NULL, 0, NULL);
        if (timer_thread == NULL) {
            printf("启动定时器线程失败\n");
            auto_run = false;
        }
    } else {
        printf("自动运行已关闭，返回手动模式\n");
        // 停止定时器线程
        if (timer_thread != NULL) {
            timer_running = false;
            WaitForSingleObject(timer_thread, 1000);  // 等待线程结���，超时1秒
            CloseHandle(timer_thread);
            timer_thread = NULL;
        }
    }
}

// 定时器线程函数
unsigned __stdcall timer_thread_func(void* arg) {
    while (timer_running && system_running) {
        run_simulation();
        Sleep(1000);  // 暂停1秒
    }
    return 0;
}

// 添加缺失的函数实现 - 显示进程
void display_processes() {
    printf("\n===== 进程列表 =====\n");
    printf("当前调度算法: %s\n", get_algorithm_name(current_algorithm));
    
    if (system_interrupt_flag) {
        printf("系统处于中断状态\n");
    }

    // 显示正在运行的进程
    if (running_process != NULL) {
        printf("\n运行中: PID=%d, 名称=%s, 优先级=%d, 时间片=%d, 内存=%d-%d\n",
               running_process->pid,
               running_process->name,
               running_process->priority,
               running_process->time_slice,
               running_process->memory_start,
               running_process->memory_start + running_process->memory_size - 1);
    } else {
        printf("\n运行中: 无\n");
    }
    
    // 显示被中断的进程
    if (interrupted_process != NULL) {
        printf("\n被中断: PID=%d, 名称=%s, 优先级=%d, 时间片=%d, 内存=%d-%d\n",
               interrupted_process->pid,
               interrupted_process->name,
               interrupted_process->priority,
               interrupted_process->time_slice,
               interrupted_process->memory_start,
               interrupted_process->memory_start + interrupted_process->memory_size - 1);
    }

    // 显示就绪队列
    printf("\n就绪队列:\n");
    PCB *current = ready_queue;
    if (current == NULL) {
        printf("空\n");
    }
    while (current != NULL) {
        printf("PID=%d, 名称=%s, 优先级=%d, 时间片=%d, 内存=%d-%d\n",
               current->pid,
               current->name,
               current->priority,
               current->time_slice,
               current->memory_start,
               current->memory_start + current->memory_size - 1);
        current = current->next;
    }

    // 显示阻塞队列
    printf("\n阻塞队列:\n");
    current = blocked_queue;
    if (current == NULL) {
        printf("空\n");
    }
    while (current != NULL) {
        printf("PID=%d, 名称=%s, 优先级=%d, 时间片=%d, 内存=%d-%d\n",
               current->pid,
               current->name,
               current->priority,
               current->time_slice,
               current->memory_start,
               current->memory_start + current->memory_size - 1);
        current = current->next;
    }

    printf("===================\n\n");
}

// 添加缺失的函数实现 - 显示内存状态
void display_memory() {
    printf("\n===== 内存使用情况 =====\n");
    MemoryBlock *current = memory;
    int free_blocks = 0;
    int used_blocks = 0;
    int free_memory = 0;
    int used_memory = 0;

    printf("起始地址\t大小\t状态\tPID\n");
    printf("--------------------------------\n");

    while (current != NULL) {
        printf("%d\t\t%d\t%s\t%d\n",
               current->start_address,
               current->size,
               current->is_allocated ? "已分配" : "空闲",
               current->pid);

        if (current->is_allocated) {
            used_blocks++;
            used_memory += current->size;
        } else {
            free_blocks++;
            free_memory += current->size;
        }

        current = current->next;
    }

    printf("\n总内存: %d\n", MEMORY_SIZE);
    printf("已使用: %d (%.2f%%)\n", used_memory, (float)used_memory / MEMORY_SIZE * 100);
    printf("空闲: %d (%.2f%%)\n", free_memory, (float)free_memory / MEMORY_SIZE * 100);
    printf("内存块数: %d (已用: %d, 空闲: %d)\n", free_blocks + used_blocks, used_blocks, free_blocks);
    printf("=======================\n\n");
}

// 添加缺失的函数实现 - 处理时钟中断
void handle_timer_interrupt() {
    time_counter++;

    // 如果有正在运行的进程，减少时间片
    if (running_process != NULL) {
        running_process->time_slice--;
        printf("时钟中断: 进程 %s (PID=%d) 剩余时间片 %d\n",
               running_process->name,
               running_process->pid,
               running_process->time_slice);

        // 如果时间片用完，进程结束
        if (running_process->time_slice <= 0) {
            printf("进程 %s (PID=%d) 已完成运行，自动终止\n",
                   running_process->name, running_process->pid);

            int pid = running_process->pid;
            // 先保存PID，然后释放PCB
            free_memory(pid);  // 先释放内存，避免调度新进程时复用
            free(running_process);
            running_process = NULL;

            // 再调度新进程
            schedule_process();
        }
    } else {
        // 没有进程在运行，尝试调度
        schedule_process();
    }

    // 随机生成I/O完成事件
    if (blocked_queue != NULL && rand() % 10 == 0) { // 10%的概率
        // 随机选择一个阻塞进程唤醒
        PCB *current = blocked_queue;
        PCB *prev = NULL;
        int count = 0;

        // 计算阻塞队列长度
        while (current != NULL) {
            count++;
            current = current->next;
        }

        if (count > 0) {
            int random_index = rand() % count;
            current = blocked_queue;
            prev = NULL;

            // 找到要唤醒的进程
            for (int i = 0; i < random_index; i++) {
                prev = current;
                current = current->next;
            }

            // 从阻塞队列移除
            if (prev == NULL) {
                blocked_queue = current->next;
            } else {
                prev->next = current->next;
            }

            printf("I/O中断: 进程 %s (PID=%d) I/O操作完成，被唤醒\n",
                   current->name, current->pid);

            // 重置状态并加入就绪队列
            current->next = NULL;
            current->state = READY;
            add_to_ready_queue(current);
        }
    }
}

// 添加缺失的函数实现 - 运行模拟
void run_simulation() {
    printf("\n===== 运行系统 (时间片 %d) =====\n", time_counter + 1);
    
    if (system_interrupt_flag && interrupted_process != NULL) {
        printf("注意：系统处于中断状态，进程 %s (PID=%d) 被挂起\n", 
               interrupted_process->name, interrupted_process->pid);
    }
    
    // 执行一次时钟中断
    handle_timer_interrupt();
    
    printf("=========================\n\n");
}

int main() {
    char command[100];

    // 禁用缓冲确保即时输出
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);

    printf("欢迎使用操作系统模拟程序 v1.0\n");
    printf("输入 'help' 查看可用命令\n");
    fflush(stdout);

    init_system();

    while (system_running) {
        printf("OS>");
        fflush(stdout);

        if (fgets(command, sizeof(command), stdin) == NULL) {
            if (feof(stdin)) {
                printf("\n检测到文件结束符，退出程序\n");
                system_running = false;
                auto_run = false;  // 确保退出时关闭自动运行
                timer_running = false;  // 停止定时器线程
            } else {
                perror("fgets错误");
            }
            continue;
        }

        // 处理换行符
        command[strcspn(command, "\n")] = '\0';

        if (strlen(command) > 0) {
            process_command(command);
        }
    }

    // 确保关闭自动运行和定时器线程
    auto_run = false;
    timer_running = false;
    
    if (timer_thread != NULL) {
        WaitForSingleObject(timer_thread, 1000);
        CloseHandle(timer_thread);
        timer_thread = NULL;
    }
    
    cleanup_system();
    printf("系统已退出\n");
    return 0;
}

// 处理系统中断（响应stop命令）
void handle_system_interrupt() {
    printf("\n[中断响应] 收到系统中断请求\n");
    
    // 检查是否有正在运行的进程
    if (running_process == NULL) {
        printf("[中断处理] 当前没有运行中的进程\n");
        return;
    }
    
    // 保存当前运行进程
    interrupted_process = running_process;
    printf("[中断处理] 进程 %s (PID=%d) 被中断挂起\n", 
           interrupted_process->name, interrupted_process->pid);
    
    // 标记中断状态
    system_interrupt_flag = true;
    
    // 将被中断进程状态设置为就绪状态，但不加入就绪队列
    interrupted_process->state = READY;
    
    // 清空当前运行进程指针
    running_process = NULL;
    
    printf("[中断处理] 进入重新调度\n");
    
    // 调度其他进程运行
    schedule_process();
}

// 恢复被中断的进程
void resume_interrupted_process() {
    if (!system_interrupt_flag || interrupted_process == NULL) {
        printf("没有被中断的进程需要恢复\n");
        return;
    }
    
    printf("\n[中断恢复] 恢复被中断的进程 %s (PID=%d)\n", 
           interrupted_process->name, interrupted_process->pid);
    
    // 如果当前有运行中的进程，先将其放回就绪队列
    if (running_process != NULL) {
        printf("[中断恢复] 当前运行中的进程 %s (PID=%d) 被放回就绪队列\n",
               running_process->name, running_process->pid);
        running_process->state = READY;
        add_to_ready_queue(running_process);
    }
    
    // 恢复被中断的进程为运行中
    running_process = interrupted_process;
    running_process->state = RUNNING;
    
    // 清除中断记录
    interrupted_process = NULL;
    system_interrupt_flag = false;
    
    printf("[中断恢复] 被中断进程已恢复运行\n");
}

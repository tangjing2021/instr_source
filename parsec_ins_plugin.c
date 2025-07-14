#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <qemu-plugin.h>
#include <capstone/capstone.h>
#include <capstone/riscv.h>
#include <glib.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static int other_logged = 0;
static void log_unknown(const cs_insn *ci) {
    if (other_logged < 100) {
        fprintf(stderr, "Unknown insn: id=%u mnemonic=%s, op_str=%s\n",
                ci->id, ci->mnemonic, ci->op_str);
        other_logged++;
    }
}

static const char *instr_type[] = {
    "Load/Store", "Int ALU", "Int Mul/Div",
    "FP ALU", "FP FMA", "FP Other",
    "Control Flow", "Atomic", "Vector", "System/CSR", "Other", "Bitmanip",
    NULL
};
#define NUM_INSTR_TYPES (sizeof(instr_type)/sizeof(instr_type[0]) - 1)

// 全局统计
static uint64_t counts[NUM_INSTR_TYPES] = {0};
static uint64_t exe_counts[NUM_INSTR_TYPES] = {0};

// 多线程统计
#define MAX_VCPUS 1024
typedef struct {
    int guest_tid;
    bool active;
    uint64_t per_vcpu_exe_counts[NUM_INSTR_TYPES];
} VCPUStats;
static pthread_mutex_t vcpu_lock;
static VCPUStats vcpu_stats_table[MAX_VCPUS];

static csh cs_handle;
static uint64_t class_time_ns[NUM_INSTR_TYPES] = {0};
static struct timespec last_ts, start_ts;
static bool time_init=false;

static intptr_t get_type_index(const char *name) {
    for (int i = 0; instr_type[i]; i++) {
        if (strcmp(instr_type[i], name) == 0) return i;
    }
    return -1;
}

static intptr_t classify_insn(const cs_insn *ci) {
    // ... (分类逻辑与你提供的可工作代码完全相同，此处省略以保持简洁) ...
    for (int i = 0; i < ci->detail->groups_count; i++) {
        switch (ci->detail->groups[i]) {
            case RISCV_GRP_HASSTDEXTM: return get_type_index("Int Mul/Div");
            case RISCV_GRP_HASSTDEXTA: return get_type_index("Atomic");
            case RISCV_GRP_HASSTDEXTF: case RISCV_GRP_HASSTDEXTD:
                switch (ci->id) {
                    case RISCV_INS_FMADD_S: case RISCV_INS_FMSUB_S: case RISCV_INS_FNMADD_S: case RISCV_INS_FNMSUB_S:
                    case RISCV_INS_FMADD_D: case RISCV_INS_FMSUB_D: case RISCV_INS_FNMADD_D: case RISCV_INS_FNMSUB_D:
                        return get_type_index("FP FMA");
                    case RISCV_INS_FADD_S: case RISCV_INS_FSUB_S: case RISCV_INS_FMUL_S: case RISCV_INS_FDIV_S:
                    case RISCV_INS_FSQRT_S: case RISCV_INS_FADD_D: case RISCV_INS_FSUB_D: case RISCV_INS_FMUL_D:
                    case RISCV_INS_FDIV_D: case RISCV_INS_FSQRT_D:
                        return get_type_index("FP ALU");
                    case RISCV_INS_FLD: case RISCV_INS_FSD: case RISCV_INS_FLW: case RISCV_INS_FSW:
                    case RISCV_INS_C_FLD: case RISCV_INS_C_FSD: case RISCV_INS_C_FLW: case RISCV_INS_C_FSW:
                    case RISCV_INS_C_FLDSP: case RISCV_INS_C_FSDSP: case RISCV_INS_C_FLWSP: case RISCV_INS_C_FSWSP:
                        return get_type_index("Load/Store");
                    default: return get_type_index("FP Other");
                }
            case RISCV_GRP_HASSTDEXTC:
                switch (ci->id) {
                    case RISCV_INS_C_FLD: case RISCV_INS_C_FSD: case RISCV_INS_C_FLW: case RISCV_INS_C_FSW:
                    case RISCV_INS_C_FLDSP: case RISCV_INS_C_FSDSP: case RISCV_INS_C_FLWSP: case RISCV_INS_C_FSWSP:
                    case RISCV_INS_C_LD: case RISCV_INS_C_SD: case RISCV_INS_C_LW: case RISCV_INS_C_SW:
                    case RISCV_INS_C_LDSP: case RISCV_INS_C_SDSP: case RISCV_INS_C_LWSP: case RISCV_INS_C_SWSP:
                        return get_type_index("Load/Store");
                    case RISCV_INS_C_BEQZ: case RISCV_INS_C_BNEZ: case RISCV_INS_C_J: case RISCV_INS_C_JR:
                    case RISCV_INS_C_JAL: case RISCV_INS_C_JALR:
                        return get_type_index("Control Flow");
                    default: return get_type_index("Int ALU");
                }
        }
    }
    switch (ci->id) {
        case RISCV_INS_JAL: case RISCV_INS_JALR: case RISCV_INS_BEQ: case RISCV_INS_BNE: case RISCV_INS_BLT:
        case RISCV_INS_BGE: case RISCV_INS_BLTU: case RISCV_INS_BGEU:
            return get_type_index("Control Flow");
        case RISCV_INS_ECALL: case RISCV_INS_EBREAK: case RISCV_INS_URET: case RISCV_INS_FENCE:
        case RISCV_INS_FENCE_I: case RISCV_INS_SFENCE_VMA: case RISCV_INS_CSRRW: case RISCV_INS_CSRRS:
        case RISCV_INS_CSRRC: case RISCV_INS_CSRRWI: case RISCV_INS_CSRRSI: case RISCV_INS_CSRRCI:
        case RISCV_INS_MRET: case RISCV_INS_SRET: case RISCV_INS_WFI:
            return get_type_index("System/CSR");
        case RISCV_INS_LB: case RISCV_INS_LH: case RISCV_INS_LW: case RISCV_INS_LD: case RISCV_INS_LBU:
        case RISCV_INS_LHU: case RISCV_INS_LWU: case RISCV_INS_SB: case RISCV_INS_SH: case RISCV_INS_SW:
        case RISCV_INS_SD:
            return get_type_index("Load/Store");
        case RISCV_INS_ADD: case RISCV_INS_SUB: case RISCV_INS_SLL: case RISCV_INS_SLT: case RISCV_INS_SLTU:
        case RISCV_INS_XOR: case RISCV_INS_SRL: case RISCV_INS_SRA: case RISCV_INS_AND: case RISCV_INS_OR:
        case RISCV_INS_ADDI: case RISCV_INS_SLTI: case RISCV_INS_SLTIU: case RISCV_INS_XORI: case RISCV_INS_ORI:
        case RISCV_INS_ANDI: case RISCV_INS_SLLI: case RISCV_INS_SRLI: case RISCV_INS_SRAI: case RISCV_INS_ADDW:
        case RISCV_INS_SUBW: case RISCV_INS_SLLW: case RISCV_INS_SRLW: case RISCV_INS_SRAW: case RISCV_INS_ADDIW:
        case RISCV_INS_SLLIW: case RISCV_INS_SRLIW: case RISCV_INS_SRAIW: case RISCV_INS_AUIPC: case RISCV_INS_LUI:
            return get_type_index("Int ALU");
        case RISCV_INS_MUL: case RISCV_INS_MULH: case RISCV_INS_MULHSU: case RISCV_INS_MULHU: case RISCV_INS_DIV:
        case RISCV_INS_DIVU: case RISCV_INS_REM: case RISCV_INS_REMU: case RISCV_INS_DIVUW: case RISCV_INS_DIVW:
        case RISCV_INS_REMUW: case RISCV_INS_REMW:
            return get_type_index("Int Mul/Div");
        default:
            const char *m = ci->mnemonic;
            if (strstr(m, "clz") || strstr(m, "ctz") || strstr(m, "cpop") || strstr(m, "rot") || strstr(m, "rev8") ||
                strstr(m, "andn") || strstr(m, "orn") || strstr(m, "xnor") || strstr(m, "min") || strstr(m, "max") ||
                strstr(m, "shfl") || strstr(m, "unshfl") || strstr(m, "bext") || strstr(m, "bdep") || strstr(m, "bfp") ||
                strstr(m, "clmul") || strstr(m, "cmov") || strstr(m, "csel") || strstr(m, "zext")) {
                return get_type_index("Bitmanip");
            }
            return -1;
    }
}


typedef struct addr_tree{
    struct addr_tree * l;
    struct addr_tree * r;
    uint64_t vaddr;
    long long  count_w,count_r,count;
}addr_tree;
addr_tree * addr_head=NULL; 
#define TREE_MAX 100000
#define COUNT_MAX 1000000      
#define TREE_TOP 30
uint32_t TREE_COUNT=0,COUNT_ADDR=0,STEP_COUNT=100000000;
addr_tree * v_tree[TREE_MAX+100]={NULL};
static void get_top(FILE *out_fp,addr_tree * h){
    uint64_t v_vaddr[TREE_TOP],v_rd[TREE_TOP],v_wr[TREE_TOP],v_c[TREE_TOP];
    uint64_t min_c=1e18;
    int min_id=-1,cnt=0;
    int v_l=0,v_r=0;
    if(h==NULL) return;
    v_tree[v_r++]=h;
    while(v_l!=v_r){
        if(cnt<TREE_TOP){
            v_vaddr[cnt]=v_tree[v_l]->vaddr;
            v_rd[cnt]=v_tree[v_l]->count_r;
            v_wr[cnt]=v_tree[v_l]->count_w;
            v_c[cnt]=v_tree[v_l]->count;
            cnt++;
        }else{
            if(min_id==-1){
                for(int i=0;i<TREE_TOP;i++){
                    if(v_c[i]<min_c){
                        min_c=v_c[i];
                        min_id=i;
                    }
                }
            }

            if(v_tree[v_l]->count>min_c){
                v_vaddr[min_id]=v_tree[v_l]->vaddr;
                v_rd[min_id]=v_tree[v_l]->count_r;
                v_wr[min_id]=v_tree[v_l]->count_w;
                v_c[min_id]=v_tree[v_l]->count;       
                min_c=v_c[0];
                min_id=0;
                for(int i=0;i<TREE_TOP;i++){
                    if(v_c[i]<min_c){
                        min_c=v_c[i];
                        min_id=i;
                    }
                }
            }

        }
        if(v_tree[v_l]->r!=NULL){
            v_tree[v_r++]=v_tree[v_l]->r;
        }
        if(v_tree[v_l]->l!=NULL){
            v_tree[v_r++]=v_tree[v_l]->l;
        }
        v_l++;
    }

    for(v_l=0;v_l<v_r;v_l++){
        free(v_tree[v_l]);
    }

    // printf("\n=== Top %d Hot Addresses ===\n", cnt);
    // printf("%-18s %-10s %-10s %-10s\n", "Address", "Reads", "Writes", "Total");
    // for (int i = 0; i < cnt; i++) {
    //     printf("0x%016" PRIx64 " %-10" PRIu64 " %-10" PRIu64 " %-10" PRIu64 "\n",
    //            v_vaddr[i], v_rd[i], v_wr[i], v_c[i]);
    // }
    fprintf(out_fp, "\n=== Top %d Hot Addresses ===\n", cnt);
    fprintf(out_fp, "%-18s %-10s %-10s %-10s\n",
            "Address", "Reads", "Writes", "Total");
    for (int i = 0; i < cnt; i++) {
        fprintf(out_fp,
                "0x%016" PRIx64 " %-10" PRIu64 " %-10" PRIu64 " %-10" PRIu64 "\n",
                v_vaddr[i], v_rd[i], v_wr[i], v_c[i]);
    }
}

static void update_tree(addr_tree * h, uint64_t vaddr , bool f){
    while(1){
        if(vaddr > h->vaddr){
            if(h->r==NULL){
                if(TREE_COUNT>=TREE_MAX) return;
                addr_tree *p=(addr_tree*)malloc(sizeof(addr_tree));
                p->l=NULL;
                p->r=NULL;
                p->vaddr=vaddr;
                p->count_w=0;
                p->count_r=0;
                p->count=0;
                if(f){
                    p->count_w++;
                }else{
                    p->count_r++;
                }
                p->count++;
                h->r=p;
                TREE_COUNT++;
                return;
            }else{
                h=h->r;
            }
        }else if(vaddr < h->vaddr){
            if(h->l==NULL){
                if(TREE_COUNT>=TREE_MAX) return;
                addr_tree *p=(addr_tree*)malloc(sizeof(addr_tree));
                p->l=NULL;
                p->r=NULL;
                p->vaddr=vaddr;
                p->count_w=0;
                p->count_r=0;
                p->count=0;
                if(f){
                    p->count_w++;
                }else{
                    p->count_r++;
                }
                p->count++;
                h->l=p;
                TREE_COUNT++;
                return;
            }else{
                h=h->l;
            }
        }else{
            if(f){
                h->count_w++;
            }else{
                h->count_r++;
            }
            h->count++;
            return;
        }
    }
}
uint64_t vaddr_same_count=0,vaddr_1_count=0,vaddr_2_count=0,vaddr_4_count=0,vaddr_8_count=0,vaddr_disct_count=0;
uint64_t last_vaddr=-1;

// 更新热点地址统计
static void update_hot_address(uint64_t vaddr, qemu_plugin_meminfo_t info) {
    if(addr_head==NULL){
        addr_head=(addr_tree*)malloc(sizeof(addr_tree));
        addr_head->l=NULL;
        addr_head->r=NULL;
        addr_head->vaddr=vaddr;
        addr_head->count=1;
        addr_head->count_w=0;
        addr_head->count_r=0;
        if(qemu_plugin_mem_is_store(info)){
            addr_head->count_w=addr_head->count_w+1;
        }else{
            addr_head->count_r=addr_head->count_r+1;
        }
        TREE_COUNT++;
    }else{
        update_tree(addr_head,vaddr,(qemu_plugin_mem_is_store(info)));
    }

}

// 内存访问回调函数
static void mem_access_cb(unsigned int vcpu_index, 
                          qemu_plugin_meminfo_t info, 
                          uint64_t vaddr,
                          void *udata) {
    uint64_t step;
    if(last_vaddr!=-1){
        if(last_vaddr<vaddr){
            step=vaddr-last_vaddr;
        }else if(last_vaddr==vaddr){
            step=0;
        }else{
            step=last_vaddr-vaddr;
        }

        if(step<=8){
            if(step==8){
                vaddr_8_count++;
            }else if(step==4){
                vaddr_4_count++;
            }else if(step==2){
                vaddr_2_count++;
            }else if(step==1){
                vaddr_1_count++;
            }else{
                vaddr_same_count++;
            }
        }else{
            vaddr_disct_count++;
        }

        last_vaddr=vaddr;
    }else{
        last_vaddr=vaddr;
    }
    if(STEP_COUNT>0){
        STEP_COUNT--;
    }else{
        if(COUNT_ADDR<COUNT_MAX){
            COUNT_ADDR++;
            update_hot_address(vaddr, info);
        }
    }

}



static void insn_exec_cb(unsigned int vcpu_index, void *ud) {
    intptr_t type_idx = (intptr_t)ud;
    if (type_idx < 0 || type_idx >= NUM_INSTR_TYPES || vcpu_index >= MAX_VCPUS) {
        return;
    }
    if(!time_init){
        time_init=true;
        clock_gettime(CLOCK_MONOTONIC, &start_ts);
        last_ts = start_ts;
    }else{
    // 1) 取当前时间，计算与上次回调的时间差（ns）
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t delta = (now.tv_sec  - last_ts.tv_sec ) * 1000000000ULL + (now.tv_nsec - last_ts.tv_nsec);
        // 2) 累加到本指令类别
        class_time_ns[type_idx] += delta;
        // 3) 更新 last_ts
        last_ts = now;
    }
    exe_counts[type_idx]++;
    vcpu_stats_table[vcpu_index].per_vcpu_exe_counts[type_idx]++;
}
static void call_insn_cb(unsigned int vcpu_index, void *udata) {
    // 1) 拿到执行到的指令句柄

}
static void tb_trans_cb(qemu_plugin_id_t id, struct qemu_plugin_tb *tb) {
    size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        cs_insn *ci;
        
        uint8_t insn_buf[16];
        // 修正: 使用 3 个参数的 qemu_plugin_insn_data
        size_t insn_size = qemu_plugin_insn_data(insn, insn_buf, sizeof(insn_buf));
        uint64_t addr = qemu_plugin_insn_vaddr(insn);

        if (cs_disasm(cs_handle, insn_buf, insn_size, addr, 1, &ci) > 0) {
            intptr_t type = classify_insn(&ci[0]);
            if (type < 0) {
                log_unknown(&ci[0]);
                type = get_type_index("Other");
            }
            counts[type]++;
            qemu_plugin_register_vcpu_insn_exec_cb(
                insn, insn_exec_cb, QEMU_PLUGIN_CB_NO_REGS, (void *)type
            );

            // 如果是加载/存储指令，注册内存访问回调
            if (type == get_type_index("Load/Store")) {
                qemu_plugin_register_vcpu_mem_cb(
                    insn,                
                    mem_access_cb,       // 回调函数
                    QEMU_PLUGIN_CB_NO_REGS,
                    QEMU_PLUGIN_MEM_RW,  
                    NULL
                );
            }
            // if (ci->id == RISCV_INS_JAL || ci->id == RISCV_INS_JALR) {
            //     qemu_plugin_register_vcpu_insn_exec_cb(
            //         insn, call_insn_cb, QEMU_PLUGIN_CB_NO_REGS, NULL
            //     );
            // }


            cs_free(ci, 1);
        }
    }
}

static void syscall_ret_cb(qemu_plugin_id_t id, unsigned int vcpu_index,
                           int64_t num, int64_t ret)
{
    // riscv64 的 clone 系统调用号是 220
    if (num == 220 && ret > 0) {
        if (vcpu_index < MAX_VCPUS) {
            pthread_mutex_lock(&vcpu_lock);
            if (!vcpu_stats_table[vcpu_index].active) {
                vcpu_stats_table[vcpu_index].active = true;
                vcpu_stats_table[vcpu_index].guest_tid = (int)ret;
            }
            pthread_mutex_unlock(&vcpu_lock);
        }
    }
}
static void exit_cb(qemu_plugin_id_t id, void *p) {
    // 先计算并打印全局动态指令总数
    FILE *f_inst  ;
    FILE *f_vaddr ;
    FILE *f_time  ;
    FILE *f_stride;
    // FILE *f_inst   = fopen("plugin_inst",   "w");
    // FILE *f_vaddr  = fopen("plugin_vaddr",  "w");
    // FILE *f_time   = fopen("plugin_time",   "w");
    // FILE *f_stride = fopen("plugin_stride", "w");
    if (!f_inst || !f_vaddr || !f_time || !f_stride) {
        perror("fopen");
        // 如果打开失败，退回到标准输出
        f_inst  = f_inst  ? f_inst  : stdout;
        f_vaddr = f_vaddr ? f_vaddr : stdout;
        f_time  = f_time  ? f_time  : stdout;
        f_stride = f_stride ? f_stride : stdout;
    }

    uint64_t global_dynamic_total = 0;
    for (int i = 0; instr_type[i]; i++) {
        global_dynamic_total += exe_counts[i];
    }
    fprintf(f_inst, "\n=== Global Instruction Counts ===\n");
    for (int i = 0; instr_type[i]; i++) {
        if (counts[i] > 0 || exe_counts[i] > 0) {
            fprintf(f_inst, "%-15s : static=%10" PRIu64 " dynamic=%10" PRIu64 "\n",
                    instr_type[i], counts[i], exe_counts[i]);
        }
    }
    fprintf(f_inst, ">>> Global Dynamic Instructions Total: %" PRIu64 " <<<\n",
    global_dynamic_total);
    
    get_top(f_vaddr,addr_head);
    // // —— 新增：打印时间统计 —— 
    // struct timespec end_ts;
    // clock_gettime(CLOCK_MONOTONIC, &end_ts);
    // uint64_t total_ns = (end_ts.tv_sec  - start_ts.tv_sec ) * 1000000000ULL
    //                   + (end_ts.tv_nsec - start_ts.tv_nsec);

    // printf("\n=== Instruction Class Timing ===\n");
    // printf("%-15s : %12s\n", "Class", "Time (ms)");
    // for (int i = 0; instr_type[i]; i++) {
    //     double ms = class_time_ns[i] / 1e6;
    //     if(ms==0) continue;
    //     printf("%-15s : %12.3f\n",
    //            instr_type[i], ms);
    // }
    // printf(">>> Total Elapsed Time: %.3f ms <<<\n", total_ns / 1e6);

    struct timespec end_ts;
    clock_gettime(CLOCK_MONOTONIC, &end_ts);
    uint64_t total_ns = (end_ts.tv_sec  - start_ts.tv_sec ) * 1000000000ULL
                      + (end_ts.tv_nsec - start_ts.tv_nsec);

    fprintf(f_time, "\n=== Instruction Class Timing ===\n");
    fprintf(f_time, "%-15s : %12s\n", "Class", "Time (ms)");
    for (int i = 0; instr_type[i]; i++) {
        double ms = class_time_ns[i] / 1e6;
        if (ms == 0) continue;
        fprintf(f_time, "%-15s : %12.3f\n",
                instr_type[i], ms);
    }
    fprintf(f_time, ">>> Total Elapsed Time: %.3f ms <<<\n", total_ns / 1e6);


    // 计算总访问数
    uint64_t total_mem_access = vaddr_same_count
        + vaddr_1_count + vaddr_2_count
        + vaddr_4_count + vaddr_8_count
        + vaddr_disct_count;

    if (total_mem_access > 0) {
        fprintf(f_stride, "\n=== Memory Access Stride Statistics ===\n");
        fprintf(f_stride, "Total memory accesses: %" PRIu64 "\n", total_mem_access);

        // 打印每种步长的统计
        fprintf(f_stride, "\n%-20s %12s %12s\n", "Stride Type", "Count", "Percentage");

        // 相同地址访问
        fprintf(f_stride, "%-20s %12" PRIu64 " %11.3f%%\n",
                "Same Address", vaddr_same_count,
                (double)vaddr_same_count / total_mem_access * 100.0);

        // 1字节步长
        fprintf(f_stride, "%-20s %12" PRIu64 " %11.3f%%\n",
                "1-byte stride", vaddr_1_count,
                (double)vaddr_1_count / total_mem_access * 100.0);

        // 2字节步长
        fprintf(f_stride, "%-20s %12" PRIu64 " %11.3f%%\n",
                "2-byte stride", vaddr_2_count,
                (double)vaddr_2_count / total_mem_access * 100.0);

        // 4字节步长
        fprintf(f_stride, "%-20s %12" PRIu64 " %11.3f%%\n",
                "4-byte stride", vaddr_4_count,
                (double)vaddr_4_count / total_mem_access * 100.0);

        // 8字节步长
        fprintf(f_stride, "%-20s %12" PRIu64 " %11.3f%%\n",
                "8-byte stride", vaddr_8_count,
                (double)vaddr_8_count / total_mem_access * 100.0);

        // 离散访问
        fprintf(f_stride, "%-20s %12" PRIu64 " %11.3f%%\n",
                "Other strides", vaddr_disct_count,
                (double)vaddr_disct_count / total_mem_access * 100.0);
    }
    // 关闭文件和清理
    // if (f_inst  != stdout) fclose(f_inst);
    // if (f_vaddr != stdout) fclose(f_vaddr);
    // if (f_time  != stdout) fclose(f_time);
    // if (f_stride != stdout) fclose(f_stride);
    // pthread_mutex_destroy(&vcpu_lock);
    cs_close(&cs_handle);
}



QEMU_PLUGIN_EXPORT int
qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                    int argc, char **argv)
{
    cs_mode mode = CS_MODE_RISCV64 | CS_MODE_RISCVC | CS_MODE_LITTLE_ENDIAN;
    if (cs_open(CS_ARCH_RISCV, mode, &cs_handle) != CS_ERR_OK) {
        fprintf(stderr, "ERROR: Capstone initialization failed\n");
        return -1;
    }
    cs_option(cs_handle, CS_OPT_DETAIL, CS_OPT_ON);

    // memset(vcpu_stats_table, 0, sizeof(vcpu_stats_table));
    // if (pthread_mutex_init(&vcpu_lock, NULL) != 0) {
    //     fprintf(stderr, "ERROR: Mutex initialization failed\n");
    //     return -1;
    // }
    
    // vcpu_stats_table[0].active = true;
    // vcpu_stats_table[0].guest_tid = (int)getpid();

    qemu_plugin_register_vcpu_tb_trans_cb(id, tb_trans_cb);
    // qemu_plugin_register_vcpu_syscall_ret_cb(id, syscall_ret_cb);
    qemu_plugin_register_atexit_cb(id, exit_cb, NULL);

    // printf("INFO: Multi-thread instruction analysis plugin installed.\n");
    return 0;
}

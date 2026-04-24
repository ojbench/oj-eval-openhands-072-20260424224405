// Minimal RV32I simulator for typical "@addr + bytes" .data format
// Supports a subset sufficient for common benchmarks: RV32I base
// Exits on ecall with a7 (x17) == 93, printing a0 (x10) as integer

#include <bits/stdc++.h>
using namespace std;

struct Mem {
    unordered_map<uint32_t, uint8_t> m;
    uint8_t rb(uint32_t a) const {
        auto it = m.find(a);
        return it == m.end() ? 0 : it->second;
    }
    uint32_t rl(uint32_t a) const { // little-endian 32-bit
        uint32_t b0 = rb(a);
        uint32_t b1 = rb(a+1);
        uint32_t b2 = rb(a+2);
        uint32_t b3 = rb(a+3);
        return b0 | (b1<<8) | (b2<<16) | (b3<<24);
    }
    void wb(uint32_t a, uint8_t v){ m[a]=v; }
    void ww(uint32_t a, uint16_t v){ m[a]=v&0xff; m[a+1]=(v>>8)&0xff; }
    void wl(uint32_t a, uint32_t v){ m[a]=v&0xff; m[a+1]=(v>>8)&0xff; m[a+2]=(v>>16)&0xff; m[a+3]=(v>>24)&0xff; }
};

static inline int32_t sext(uint32_t x, int bits){
    uint32_t m = 1u << (bits-1);
    return (int32_t)((x ^ m) - m);
}

int main(){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    // Load .data format: lines with "@<hexaddr>" then hex bytes
    Mem mem;
    uint32_t cur = 0;
    uint32_t min_addr = 0xffffffffu, max_addr = 0;
    string s;
    while (cin >> s) {
        if (!s.empty() && s[0]=='@') {
            // address
            cur = (uint32_t)stoul(s.substr(1), nullptr, 16);
            min_addr = min(min_addr, cur);
        } else {
            // byte
            if (s.size()>=2) {
                uint8_t v = (uint8_t)stoul(s, nullptr, 16);
                mem.wb(cur, v);
                max_addr = max(max_addr, cur);
                ++cur;
            }
        }
    }

    uint32_t x[32]{}; // integer registers
    uint32_t pc = (min_addr==0xffffffffu)? 0u : min_addr;
    // Initialize stack pointer to just above program region
    uint32_t stack_base = (max_addr==0? 0x00100000u : (max_addr + 0x00010000u));
    if (stack_base < 0x00100000u) stack_base = 0x00100000u; // at least 1 MiB
    x[2] = stack_base & ~0xfu; // x2 = sp, 16-byte aligned
    bool want_exit = false;
    auto LOAD8 = [&](uint32_t a){ return mem.rb(a); };
    auto LOAD16 = [&](uint32_t a){ return (uint16_t)(mem.rb(a) | (mem.rb(a+1)<<8)); };
    auto LOAD32 = [&](uint32_t a){ return mem.rl(a); };
    auto STORE8 = [&](uint32_t a, uint8_t v){
        if (a == 0x10000000u || a == 0x00030000u) { cout << (char)v; return; }
        if (a == 0x10000004u || a == 0x00030004u) { want_exit = true; return; }
        mem.wb(a,v);
    };
    auto STORE16 = [&](uint32_t a, uint16_t v){
        if (a == 0x10000000u || a == 0x00030000u) { cout << (char)(v & 0xff); return; }
        if (a == 0x10000004u || a == 0x00030004u) { want_exit = true; return; }
        mem.ww(a,v);
    };
    auto STORE32 = [&](uint32_t a, uint32_t v){
        if (a == 0x10000000u || a == 0x00030000u) { cout << (char)(v & 0xff); return; }
        if (a == 0x10000004u || a == 0x00030004u) { want_exit = true; return; }
        mem.wl(a,v);
    };

    const uint64_t MAX_STEPS = 200000000ull; // cap to avoid infinite loops, allow longer programs
    uint64_t steps = 0;
    auto get_imm_i = [&](uint32_t ins){ return sext(ins>>20, 12); };
    auto get_imm_s = [&](uint32_t ins){
        uint32_t imm = ((ins>>7)&0x1f) | (((ins>>25)&0x7f)<<5);
        return sext(imm, 12);
    };
    auto get_imm_b = [&](uint32_t ins){
        uint32_t imm = ((ins>>7)&0x1)<<11 | ((ins>>8)&0xf)<<1 | ((ins>>25)&0x3f)<<5 | ((ins>>31)&0x1)<<12;
        return sext(imm, 13);
    };
    auto get_imm_u = [&](uint32_t ins){ return (int32_t)(ins & 0xfffff000u); };
    auto get_imm_j = [&](uint32_t ins){
        uint32_t imm = ((ins>>21)&0x3ff)<<1 | ((ins>>20)&1)<<11 | ((ins>>12)&0xff)<<12 | ((ins>>31)&1)<<20;
        return sext(imm, 21);
    };

    int printed = 0; // track if we printed output upon exit
    while (steps++ < MAX_STEPS) {
        uint32_t ins = LOAD32(pc);
        uint32_t op = ins & 0x7f;
        uint32_t rd = (ins>>7)&0x1f;
        uint32_t funct3 = (ins>>12)&7;
        uint32_t rs1 = (ins>>15)&0x1f;
        uint32_t rs2 = (ins>>20)&0x1f;
        uint32_t funct7 = (ins>>25)&0x7f;
        uint32_t next_pc = pc + 4;
        uint32_t val = 0;
        switch (op) {
            case 0x37: // LUI
                x[rd] = ins & 0xfffff000u;
                break;
            case 0x17: // AUIPC
                x[rd] = pc + (ins & 0xfffff000u);
                break;
            case 0x6f: { // JAL
                int32_t imm = get_imm_j(ins);
                x[rd] = next_pc;
                next_pc = pc + imm;
                break;
            }
            case 0x67: { // JALR
                int32_t imm = get_imm_i(ins);
                uint32_t t = (x[rs1] + imm) & ~1u;
                x[rd] = next_pc;
                next_pc = t;
                break;
            }
            case 0x63: { // BRANCH
                int32_t imm = get_imm_b(ins);
                uint32_t a = x[rs1], b = x[rs2];
                bool take = false;
                switch (funct3) {
                    case 0: take = (a==b); break; // BEQ
                    case 1: take = (a!=b); break; // BNE
                    case 4: take = ((int32_t)a < (int32_t)b); break; // BLT
                    case 5: take = ((int32_t)a >= (int32_t)b); break; // BGE
                    case 6: take = (a < b); break; // BLTU
                    case 7: take = (a >= b); break; // BGEU
                    default: break;
                }
                if (take) next_pc = pc + imm;
                break;
            }
            case 0x03: { // LOAD
                int32_t imm = get_imm_i(ins);
                uint32_t addr = x[rs1] + imm;
                switch (funct3) {
                    case 0: val = sext(LOAD8(addr), 8); break; // LB
                    case 1: val = sext(LOAD16(addr), 16); break; // LH
                    case 2: val = LOAD32(addr); break; // LW
                    case 4: val = (uint32_t)LOAD8(addr); break; // LBU
                    case 5: val = (uint32_t)LOAD16(addr); break; // LHU
                    default: val = 0; break;
                }
                x[rd] = val;
                break;
            }
            case 0x23: { // STORE
                int32_t imm = get_imm_s(ins);
                uint32_t addr = x[rs1] + imm;
                uint32_t data = x[rs2];
                switch (funct3) {
                    case 0: STORE8(addr, data & 0xff); break; // SB
                    case 1: STORE16(addr, data & 0xffff); break; // SH
                    case 2: STORE32(addr, data); break; // SW
                    default: break;
                }
                break;
            }
            case 0x13: { // OP-IMM
                int32_t imm = get_imm_i(ins);
                switch (funct3) {
                    case 0: val = x[rs1] + imm; break; // ADDI
                    case 2: val = (int32_t)x[rs1] < imm; break; // SLTI
                    case 3: val = x[rs1] < (uint32_t)imm; break; // SLTIU
                    case 4: val = x[rs1] ^ (uint32_t)imm; break; // XORI
                    case 6: val = x[rs1] | (uint32_t)imm; break; // ORI
                    case 7: val = x[rs1] & (uint32_t)imm; break; // ANDI
                    case 1: { // SLLI
                        uint32_t sh = (ins >> 20) & 0x1f; val = x[rs1] << sh; break;
                    }
                    case 5: { // SRLI/SRAI
                        uint32_t sh = (ins >> 20) & 0x1f;
                        if (funct7==0x20) val = (uint32_t)((int32_t)x[rs1] >> sh); // SRAI
                        else val = x[rs1] >> sh; // SRLI
                        break;
                    }
                    default: break;
                }
                x[rd] = val;
                break;
            }
            case 0x33: { // OP
                if (funct7 == 0x01) { // M extension
                    switch (funct3) {
                        case 0: { // MUL
                            val = (uint32_t)((int64_t)(int32_t)x[rs1] * (int64_t)(int32_t)x[rs2]);
                            break;
                        }
                        case 1: { // MULH
                            int64_t a = (int64_t)(int32_t)x[rs1];
                            int64_t b = (int64_t)(int32_t)x[rs2];
                            int64_t prod = a * b;
                            val = (uint32_t)((uint64_t)prod >> 32);
                            break;
                        }
                        case 2: { // MULHSU
                            int64_t a = (int64_t)(int32_t)x[rs1];
                            uint64_t b = (uint64_t)x[rs2];
                            __int128 prod = (__int128)a * (__int128)b;
                            val = (uint32_t)((uint64_t)(prod >> 32));
                            break;
                        }
                        case 3: { // MULHU
                            uint64_t a = (uint64_t)x[rs1];
                            uint64_t b = (uint64_t)x[rs2];
                            __int128 prod = (__int128)a * (__int128)b;
                            val = (uint32_t)((uint64_t)(prod >> 32));
                            break;
                        }
                        case 4: { // DIV
                            int32_t a = (int32_t)x[rs1];
                            int32_t b = (int32_t)x[rs2];
                            if (b == 0) val = 0xffffffffu;
                            else if (a == INT32_MIN && b == -1) val = (uint32_t)INT32_MIN;
                            else val = (uint32_t)(a / b);
                            break;
                        }
                        case 5: { // DIVU
                            uint32_t a = x[rs1];
                            uint32_t b = x[rs2];
                            val = (b == 0)? 0xffffffffu : (a / b);
                            break;
                        }
                        case 6: { // REM
                            int32_t a = (int32_t)x[rs1];
                            int32_t b = (int32_t)x[rs2];
                            if (b == 0) val = (uint32_t)a;
                            else if (a == INT32_MIN && b == -1) val = 0;
                            else val = (uint32_t)(a % b);
                            break;
                        }
                        case 7: { // REMU
                            uint32_t a = x[rs1];
                            uint32_t b = x[rs2];
                            val = (b == 0)? a : (a % b);
                            break;
                        }
                        default: break;
                    }
                } else {
                    switch (funct3) {
                        case 0: val = (funct7==0x20)? (x[rs1] - x[rs2]) : (x[rs1] + x[rs2]); break; // SUB/ADD
                        case 1: val = x[rs1] << (x[rs2] & 0x1f); break; // SLL
                        case 2: val = (int32_t)x[rs1] < (int32_t)x[rs2]; break; // SLT
                        case 3: val = x[rs1] < x[rs2]; break; // SLTU
                        case 4: val = x[rs1] ^ x[rs2]; break; // XOR
                        case 5: val = (funct7==0x20)? (uint32_t)((int32_t)x[rs1] >> (x[rs2]&0x1f)) : (x[rs1] >> (x[rs2]&0x1f)); break; // SRA/SRL
                        case 6: val = x[rs1] | x[rs2]; break; // OR
                        case 7: val = x[rs1] & x[rs2]; break; // AND
                        default: break;
                    }
                }
                x[rd] = val;
                break;
            }
            case 0x0f: // FENCE (treated as NOP)
                break;
            case 0x73: { // SYSTEM
                uint32_t imm12 = ins >> 20;
                if (funct3==0 && imm12==0) { // ECALL
                    // By convention, a7=x17 is syscall id
                    uint32_t a7 = x[17], a0 = x[10];
                    switch (a7) {
                        case 93u: // exit with code in a0
                            cout << (int32_t)a0;
                            printed = 1;
                            return 0;
                        case 10u: // exit (NJU/others)
                            cout << (int32_t)a0;
                            printed = 1;
                            return 0;
                        case 1u: // print integer in a0
                            cout << (int32_t)a0;
                            break;
                        case 4u: { // print string at a0
                            uint32_t p = a0;
                            while (true) {
                                char c = (char)LOAD8(p++);
                                if (!c) break;
                                cout << c;
                            }
                            break;
                        }
                        case 11u: // putch(a0)
                            cout << (char)(a0 & 0xff);
                            break;
                        default:
                            break; // ignore others
                    }
                }
                break;
            }
            default:
                // Unknown opcode: treat as NOP to keep going
                break;
        }
        x[0] = 0; // enforce x0=0
        pc = next_pc;
        if (want_exit) { cout << (int32_t)x[10]; printed = 1; return 0; }
    }
    // If we exit loop without hitting exit ecall, print a0 for best effort
    if (!printed) cout << (int32_t)x[10];
    return 0;
}

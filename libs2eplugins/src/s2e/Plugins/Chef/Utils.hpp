#include <s2e/Utils.h>

#ifndef S2E_UTILS_HPP
#define S2E_UTILS_HPP

namespace s2e {
    struct hexstring {
        hexstring(const std::string str) : value(str) {}

        std::string value;
    };

    inline llvm::raw_ostream &operator<<(llvm::raw_ostream &out, const hexstring &h) {
        for (char ch : h.value) {
            out << hexval(ch);
        }
        return out;
    }

    inline std::ostream &operator<<(std::ostream &out, const hexstring &h) {
        for (char ch : h.value) {
            out << hexval(ch);
        }
        return out;
    }

    struct HighLevelInstruction {
        uint32_t opcode;
        uint32_t pc;
        uint32_t line;
        unsigned char function[61];
        unsigned char filename[61];
    };
}

#endif // S2E_UTILS_HPP

// Copyright 2026 hha0x617
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <string>
#include <cstdint>
#include <cctype>
#include <cstdlib>
#include "MC68030.h"

namespace Em68030::Core {

/// Parse a register name and return its current value.
/// Supports: D0-D7, A0-A7, PC, SR, SP
inline bool ParseRegisterValue(const std::string& name, const MC68030& cpu, uint32_t& outVal)
{
    if (name.size() == 2)
    {
        char c0 = static_cast<char>(std::toupper(name[0]));
        char c1 = name[1];
        if (c0 == 'D' && c1 >= '0' && c1 <= '7') { outVal = cpu.D[c1 - '0']; return true; }
        if (c0 == 'A' && c1 >= '0' && c1 <= '7') { outVal = cpu.A[c1 - '0']; return true; }
        if (c0 == 'S' && (c1 == 'R' || c1 == 'r')) { outVal = cpu.SR; return true; }
        if (c0 == 'P' && (c1 == 'C' || c1 == 'c')) { outVal = cpu.PC; return true; }
    }
    if (name == "SP" || name == "sp") { outVal = cpu.A[7]; return true; }
    return false;
}

/// Parse a numeric literal: decimal, 0x hex, or $hex.
inline bool ParseNumber(const std::string& s, size_t pos, size_t& endPos, uint32_t& outVal)
{
    if (pos >= s.size()) return false;

    size_t start = pos;
    int base = 10;
    if (pos + 1 < s.size() && s[pos] == '0' && (s[pos + 1] == 'x' || s[pos + 1] == 'X'))
    {
        base = 16; start = pos + 2;
    }
    else if (s[pos] == '$')
    {
        base = 16; start = pos + 1;
    }

    if (start >= s.size()) return false;
    char* end = nullptr;
    unsigned long long val = std::strtoull(s.c_str() + start, &end, base);
    if (end == s.c_str() + start) return false;
    endPos = static_cast<size_t>(end - s.c_str());
    outVal = static_cast<uint32_t>(val);
    return true;
}

/// Parse an address expression that may contain + or - operators.
/// Supports: register, number, register+number, number+register, register+register, etc.
/// Examples: "A7", "0x1000", "A7+12", "A7+0xC", "A7-4", "$1000+A0"
inline bool ParseAddrExpression(const std::string& rawExpr, const MC68030& cpu, uint32_t& outVal)
{
    // Trim whitespace
    size_t start = 0, end = rawExpr.size();
    while (start < end && rawExpr[start] == ' ') start++;
    while (end > start && rawExpr[end - 1] == ' ') end--;
    std::string expr = rawExpr.substr(start, end - start);
    if (expr.empty()) return false;

    // Find + or - operator (skip leading $ or 0x)
    size_t opPos = std::string::npos;
    for (size_t k = 1; k < expr.size(); k++)
    {
        if (expr[k] == '+' || expr[k] == '-')
        {
            // Make sure this isn't part of 0x prefix
            if (k >= 2 && (expr[k-1] == 'x' || expr[k-1] == 'X') && expr[k-2] == '0')
                continue;
            opPos = k;
            break;
        }
    }

    if (opPos == std::string::npos)
    {
        // No operator — simple value
        size_t dummy;
        if (ParseNumber(expr, 0, dummy, outVal)) return true;
        return ParseRegisterValue(expr, cpu, outVal);
    }

    // Split into left and right around operator
    std::string leftStr = expr.substr(0, opPos);
    std::string rightStr = expr.substr(opPos + 1);
    char op = expr[opPos];

    // Trim whitespace
    while (!leftStr.empty() && leftStr.back() == ' ') leftStr.pop_back();
    size_t rs = 0;
    while (rs < rightStr.size() && rightStr[rs] == ' ') rs++;
    if (rs > 0) rightStr = rightStr.substr(rs);

    uint32_t leftVal = 0, rightVal = 0;
    size_t dummy;
    if (!ParseNumber(leftStr, 0, dummy, leftVal))
        if (!ParseRegisterValue(leftStr, cpu, leftVal)) return false;
    if (!ParseNumber(rightStr, 0, dummy, rightVal))
        if (!ParseRegisterValue(rightStr, cpu, rightVal)) return false;

    outVal = (op == '+') ? (leftVal + rightVal) : (leftVal - rightVal);
    return true;
}

/// Evaluate a single comparison expression (no || or &&).
inline bool EvaluateSingleCondition(const std::string& cond, MC68030& cpu, Memory& memory)
{
    if (cond.empty()) return true;

    size_t i = 0;
    while (i < cond.size() && cond[i] == ' ') i++;
    if (i >= cond.size()) return true;

    uint32_t lhs = 0;
    size_t afterLhs = i;

    if (cond[i] == '[')
    {
        size_t closeBracket = cond.find(']', i + 1);
        if (closeBracket == std::string::npos) return true;

        std::string addrExpr = cond.substr(i + 1, closeBracket - i - 1);
        uint32_t addr = 0;
        if (!ParseAddrExpression(addrExpr, cpu, addr))
            return true;

        afterLhs = closeBracket + 1;
        if (afterLhs + 1 < cond.size() && cond[afterLhs] == '.')
        {
            char sz = static_cast<char>(std::tolower(cond[afterLhs + 1]));
            afterLhs += 2;
            uint32_t pa = cpu.TranslateAddress(addr);
            if (sz == 'b') lhs = memory.ReadByte(pa);
            else if (sz == 'l') lhs = memory.ReadLong(pa);
            else lhs = memory.ReadWord(pa);
        }
        else
        {
            lhs = memory.ReadWord(cpu.TranslateAddress(addr));
        }
    }
    else
    {
        size_t j = i;
        while (j < cond.size() && std::isalnum(static_cast<unsigned char>(cond[j]))) j++;
        std::string token = cond.substr(i, j - i);

        if (ParseRegisterValue(token, cpu, lhs))
            afterLhs = j;
        else if (ParseNumber(cond, i, afterLhs, lhs))
        { }
        else
            return true;
    }

    while (afterLhs < cond.size() && cond[afterLhs] == ' ') afterLhs++;
    if (afterLhs >= cond.size()) return lhs != 0;

    std::string op;
    if (cond[afterLhs] == '=' && afterLhs + 1 < cond.size() && cond[afterLhs + 1] == '=')
        { op = "=="; afterLhs += 2; }
    else if (cond[afterLhs] == '!' && afterLhs + 1 < cond.size() && cond[afterLhs + 1] == '=')
        { op = "!="; afterLhs += 2; }
    else if (cond[afterLhs] == '<' && afterLhs + 1 < cond.size() && cond[afterLhs + 1] == '=')
        { op = "<="; afterLhs += 2; }
    else if (cond[afterLhs] == '>' && afterLhs + 1 < cond.size() && cond[afterLhs + 1] == '=')
        { op = ">="; afterLhs += 2; }
    else if (cond[afterLhs] == '<')
        { op = "<"; afterLhs += 1; }
    else if (cond[afterLhs] == '>')
        { op = ">"; afterLhs += 1; }
    else if (cond[afterLhs] == '&')
    {
        afterLhs += 1;
        while (afterLhs < cond.size() && cond[afterLhs] == ' ') afterLhs++;
        uint32_t mask = 0;
        size_t afterMask;
        if (!ParseNumber(cond, afterLhs, afterMask, mask)) return true;
        lhs = lhs & mask;

        afterLhs = afterMask;
        while (afterLhs < cond.size() && cond[afterLhs] == ' ') afterLhs++;
        if (afterLhs >= cond.size()) return lhs != 0;

        if (cond[afterLhs] == '=' && afterLhs + 1 < cond.size() && cond[afterLhs + 1] == '=')
            { op = "=="; afterLhs += 2; }
        else if (cond[afterLhs] == '!' && afterLhs + 1 < cond.size() && cond[afterLhs + 1] == '=')
            { op = "!="; afterLhs += 2; }
        else return lhs != 0;
    }
    else if ((cond[afterLhs] == 'I' || cond[afterLhs] == 'i') &&
             afterLhs + 1 < cond.size() &&
             (cond[afterLhs + 1] == 'N' || cond[afterLhs + 1] == 'n') &&
             (afterLhs + 2 >= cond.size() || cond[afterLhs + 2] == ' ' || cond[afterLhs + 2] == '{'))
    {
        // IN {val1, val2, ...}
        afterLhs += 2;
        while (afterLhs < cond.size() && cond[afterLhs] == ' ') afterLhs++;
        if (afterLhs >= cond.size() || cond[afterLhs] != '{') return true;
        size_t closeBrace = cond.find('}', afterLhs + 1);
        if (closeBrace == std::string::npos) return true;
        std::string setStr = cond.substr(afterLhs + 1, closeBrace - afterLhs - 1);
        // Parse comma-separated values
        size_t pos = 0;
        while (pos < setStr.size())
        {
            while (pos < setStr.size() && (setStr[pos] == ' ' || setStr[pos] == ',')) pos++;
            if (pos >= setStr.size()) break;
            uint32_t val = 0;
            size_t end;
            // Try register name first
            size_t tokEnd = pos;
            while (tokEnd < setStr.size() && std::isalnum(static_cast<unsigned char>(setStr[tokEnd]))) tokEnd++;
            std::string tok = setStr.substr(pos, tokEnd - pos);
            if (ParseRegisterValue(tok, cpu, val))
                pos = tokEnd;
            else if (ParseNumber(setStr, pos, end, val))
                pos = end;
            else
                return true;
            if (lhs == val) return true;
        }
        return false;
    }
    else
    {
        return true;
    }

    while (afterLhs < cond.size() && cond[afterLhs] == ' ') afterLhs++;

    uint32_t rhs = 0;
    size_t j = afterLhs;
    while (j < cond.size() && std::isalnum(static_cast<unsigned char>(cond[j]))) j++;
    std::string rhsToken = cond.substr(afterLhs, j - afterLhs);
    if (!ParseRegisterValue(rhsToken, cpu, rhs))
    {
        size_t afterRhs;
        if (!ParseNumber(cond, afterLhs, afterRhs, rhs))
            return true;
    }

    if (op == "==") return lhs == rhs;
    if (op == "!=") return lhs != rhs;
    if (op == "<")  return lhs < rhs;
    if (op == ">")  return lhs > rhs;
    if (op == "<=") return lhs <= rhs;
    if (op == ">=") return lhs >= rhs;
    return true;
}

/// Split a string by a 2-char delimiter, respecting [...], {...}, and (...) nesting.
inline std::vector<std::string> SplitOutsideNesting(const std::string& s, char d0, char d1)
{
    std::vector<std::string> parts;
    size_t start = 0;
    int depth = 0; // tracks [, {, ( nesting
    for (size_t k = 0; k < s.size(); k++)
    {
        char c = s[k];
        if (c == '[' || c == '{' || c == '(') depth++;
        else if (c == ']' || c == '}' || c == ')') depth--;
        else if (depth == 0 && k + 1 < s.size() && c == d0 && s[k + 1] == d1)
        {
            parts.push_back(s.substr(start, k - start));
            k++; // skip second char
            start = k + 1;
        }
    }
    parts.push_back(s.substr(start));
    return parts;
}

/// Evaluate a leaf clause: if it's "(expr)", recurse; otherwise call EvaluateSingleCondition.
inline bool EvaluateLeaf(const std::string& rawClause, MC68030& cpu, Memory& memory);

/// Evaluate a condition expression with support for ||, &&, (), and IN {}.
inline bool EvaluateCondition(const std::string& cond, MC68030& cpu, Memory& memory)
{
    if (cond.empty()) return true;

    // Split by || (OR)
    auto orClauses = SplitOutsideNesting(cond, '|', '|');

    for (const auto& orClause : orClauses)
    {
        // Split by && (AND)
        auto andClauses = SplitOutsideNesting(orClause, '&', '&');

        bool allTrue = true;
        for (const auto& clause : andClauses)
        {
            if (!EvaluateLeaf(clause, cpu, memory))
            {
                allTrue = false;
                break;
            }
        }
        if (allTrue) return true;
    }
    return false;
}

inline bool EvaluateLeaf(const std::string& rawClause, MC68030& cpu, Memory& memory)
{
    // Trim whitespace
    size_t s = 0, e = rawClause.size();
    while (s < e && rawClause[s] == ' ') s++;
    while (e > s && rawClause[e - 1] == ' ') e--;
    if (s >= e) return true;

    // Check for (...) grouping — strip outer parens and recurse
    if (rawClause[s] == '(' && rawClause[e - 1] == ')')
    {
        // Verify the parens are matched (not just coincidental)
        int depth = 0;
        bool matched = true;
        for (size_t k = s; k < e; k++)
        {
            if (rawClause[k] == '(') depth++;
            else if (rawClause[k] == ')') depth--;
            if (depth == 0 && k < e - 1) { matched = false; break; }
        }
        if (matched)
            return EvaluateCondition(rawClause.substr(s + 1, e - s - 2), cpu, memory);
    }

    return EvaluateSingleCondition(rawClause.substr(s, e - s), cpu, memory);
}

} // namespace Em68030::Core

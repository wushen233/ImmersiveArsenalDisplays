#include "pch.h"
#include "UIConditionTreeSignature.h"

namespace IAD::UI {
    namespace {
        void Append(std::string& a_output, const IAD::ConditionNode& a_node)
        {
            a_output.push_back(a_node.isGroup ? 'G' : 'C');
            a_output.push_back(a_node.isAnd ? 'A' : 'O');
            a_output.push_back(a_node.isNot ? 'N' : 'P');
            a_output.push_back(a_node.expected ? 'T' : 'F');

            const auto appendString = [&a_output](const std::string& a_value) {
                a_output += std::to_string(a_value.size());
                a_output.push_back(':');
                a_output += a_value;
                a_output.push_back('|');
            };

            appendString(a_node.type);
            appendString(a_node.keyword);
            appendString(a_node.keyword2);
            a_output += std::to_string(a_node.children.size());
            a_output.push_back('[');
            for (const auto& child : a_node.children) {
                Append(a_output, child);
            }
            a_output.push_back(']');
        }
    }

    std::string UIConditionTreeSignature::Build(const IAD::ConditionNode& a_node)
    {
        std::string signature;
        Append(signature, a_node);
        return signature;
    }
}

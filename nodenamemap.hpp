#ifndef NODE_NAME_MAP_HPP
#define NODE_NAME_MAP_HPP

#include <unordered_map>
#include <string>
#include <mutex>

class NodeNameMap {
   public:
    void setNodeName(uint32_t nodeId, const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        nodeNames_[nodeId] = name;
    }

    std::string getNodeName(uint32_t nodeId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeNames_.find(nodeId);
        char buf[11];
        snprintf(buf, sizeof(buf), "!%08X", nodeId);
        if (it != nodeNames_.end()) {
            return it->second + " (" + std::string(buf) + ")";
        } else {
            return "(" + std::string(buf) + ")";
        }
    }

   private:
    std::unordered_map<uint32_t, std::string> nodeNames_;
    std::mutex mutex_;
};

#endif  // NODE_NAME_MAP_HPP
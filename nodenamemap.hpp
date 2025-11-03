#ifndef NODE_NAME_MAP_HPP
#define NODE_NAME_MAP_HPP

#include <unordered_map>
#include <string>
#include <mutex>
#include <functional>
class NodeNameMap {
   public:
    void setNodeName(uint32_t nodeId, const std::string& name, uint32_t cnt = 4294967295) {
        std::lock_guard<std::mutex> lock(mutex_);
        nodeNames_[nodeId] = name;
        if (cnt != 4294967295) {
            nodeMsgCnt_[nodeId] = cnt;
        } else {
            if (nodeMsgCnt_.find(nodeId) == nodeMsgCnt_.end()) {
                nodeMsgCnt_[nodeId] = 0;
            }
        }
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

    void incrementMessageCount(uint32_t nodeId) {
        std::lock_guard<std::mutex> lock(mutex_);
        nodeMsgCnt_[nodeId]++;
    }

    void resetMessageCount() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& pair : nodeMsgCnt_) {
            pair.second = 0;
        }
    }

    void saveMessageCounts(std::function<void(uint32_t, uint32_t)> saveFunc) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& pair : nodeMsgCnt_) {
            saveFunc(pair.first, pair.second);
        }
    }

   private:
    std::unordered_map<uint32_t, std::string> nodeNames_;
    std::unordered_map<uint32_t, uint32_t> nodeMsgCnt_;
    std::mutex mutex_;
};

#endif  // NODE_NAME_MAP_HPP
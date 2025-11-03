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
            nodeMsgCnt_[nodeId] = 0;
            nodeTraceCnt_[nodeId] = 0;
            nodeTelemetryCnt_[nodeId] = 0;
            nodeInfoCnt_[nodeId] = 0;
            posCnt_[nodeId] = 0;
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

    void incrementTraceCount(uint32_t nodeId) {
        std::lock_guard<std::mutex> lock(mutex_);
        nodeTraceCnt_[nodeId]++;
    }

    void incrementTelemetryCount(uint32_t nodeId) {
        std::lock_guard<std::mutex> lock(mutex_);
        nodeTelemetryCnt_[nodeId]++;
    }

    void incrementNodeInfoCount(uint32_t nodeId) {
        std::lock_guard<std::mutex> lock(mutex_);
        nodeInfoCnt_[nodeId]++;
    }

    void incrementPositionCount(uint32_t nodeId) {
        std::lock_guard<std::mutex> lock(mutex_);
        posCnt_[nodeId]++;
    }

    void resetMessageCount() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& pair : nodeMsgCnt_) {
            pair.second = 0;
        }
        for (auto& pair : nodeTraceCnt_) {
            pair.second = 0;
        }
        for (auto& pair : nodeTelemetryCnt_) {
            pair.second = 0;
        }
        for (auto& pair : nodeInfoCnt_) {
            pair.second = 0;
        }
        for (auto& pair : posCnt_) {
            pair.second = 0;
        }
    }

    void saveMessageCounts(std::function<void(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)> saveFunc) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& pair : nodeMsgCnt_) {
            saveFunc(pair.first, pair.second, nodeTraceCnt_[pair.first], nodeTelemetryCnt_[pair.first], nodeInfoCnt_[pair.first], posCnt_[pair.first]);
        }
    }

   private:
    std::unordered_map<uint32_t, std::string> nodeNames_;
    std::unordered_map<uint32_t, uint32_t> nodeMsgCnt_;
    std::unordered_map<uint32_t, uint32_t> nodeTraceCnt_;
    std::unordered_map<uint32_t, uint32_t> nodeTelemetryCnt_;
    std::unordered_map<uint32_t, uint32_t> nodeInfoCnt_;
    std::unordered_map<uint32_t, uint32_t> posCnt_;

    std::mutex mutex_;
};

#endif  // NODE_NAME_MAP_HPP
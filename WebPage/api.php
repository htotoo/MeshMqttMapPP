<?php

// Set headers for JSON output and no caching
header("Content-Type: application/json");
header("Cache-Control: no-store, no-cache, must-revalidate, max-age=0");
header("Cache-Control: post-check=0, pre-check=0", false);
header("Pragma: no-cache");
header("Expires: 0");

// Database path (same as map2.php)
$db_path = '/home/totoo/projects/meshlogger/build/nodes.db';

// Role map (from map2.php)
$ROLE_MAP_LONG = [
    0 => 'Client', 1 => 'Client Mute', 2 => 'Router', 3 => 'Router Client',
    4 => 'Repeater', 5 => 'Tracker', 6 => 'Sensor', 7 => 'TAK',
    8 => 'Client Hidden', 9 => 'Lost and Found', 10 => 'TAK Tracker', 11 => 'Router Late', 12 => 'Client base'
];

// Default response
$response = [
    'status' => 'error',
    'message' => 'Invalid request. No action specified.'
];

try {
    // Connect to the database
    $db = new PDO('sqlite:' . $db_path);
    $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    $db->setAttribute(PDO::ATTR_TIMEOUT, 5);

    $action = $_GET['action'] ?? null;
    $query = $_GET['query'] ?? null;

    switch ($action) {
        
        /**
         * Action: globalstats
         * Returns comprehensive mesh stats for a given frequency.
         * Param: query (string) - "433" or "868"
         */
        case 'globalstats':
            if ($query !== '433' && $query !== '868') {
                $response['message'] = 'Invalid query parameter. Must be "433" or "868".';
                break;
            }
            
            $query_freq = (int)$query;

            // --- 1. Fetch mainstats ---
            $allCntKey = "allcnt_$query_freq";
            $decodedKey = "decoded_$query_freq";
            $handledKey = "handled_$query_freq";
            $stmt_main = $db->prepare("SELECT {$allCntKey}, {$decodedKey}, {$handledKey}, time FROM mainstats ORDER BY time DESC LIMIT 5");
            $stmt_main->execute();
            $mainStatsData = $stmt_main->fetchAll(PDO::FETCH_ASSOC);

            $formattedMainStats = [];
            foreach ($mainStatsData as $row) {
                $formattedMainStats[] = [
                    'time_utc' => $row['time'],
                    'all_packets' => (int)($row[$allCntKey] ?? 0),
                    'decoded' => (int)($row[$decodedKey] ?? 0),
                    'handled' => (int)($row[$handledKey] ?? 0)
                ];
            }

            // --- 2. Fetch all nodes (logic from map2.php) ---
            $nodes = [];
            $nodesById = [];
            $stmt_nodes = $db->query('SELECT node_id, short_name, long_name, latitude, longitude, last_updated, battery_level, temperature, freq, role, battery_voltage, uptime, msgcntph, tracecntph, telemetrycntph, nodeinfocntph, poscntph, sumcntph, chutil, lashchn FROM nodes');
            $rows = $stmt_nodes->fetchAll(PDO::FETCH_ASSOC);
            foreach ($rows as $row) {
                $is_stale = false;
                if (!empty($row['last_updated'])) {
                    $utc = new DateTimeZone('UTC');
                    $last_seen = new DateTime($row['last_updated'], $utc);
                    $now = new DateTime('now', $utc);
                    $diff_seconds = $now->getTimestamp() - $last_seen->getTimestamp();
                    if ($diff_seconds > 86400) {
                        $is_stale = true;
                    }
                }
                $hex = sprintf('%x', $row['node_id']);
                $short_hex = substr($hex, -8);
                
                $node_data = [
                    'node_id' => (int)$row['node_id'],
                    'node_id_hex' => '!' . $short_hex,
                    'short_name' => $row['short_name'] ?? 'N/A',
                    'long_name' => $row['long_name'] ?? 'N/A',
                    'latitude' => $row['latitude'] / 10000000.0,
                    'longitude' => $row['longitude'] / 10000000.0,
                    'last_updated' => $row['last_updated'],
                    'battery_level' => (int)($row['battery_level'] ?? 0),
                    'battery_voltage' => (float)($row['battery_voltage'] ?? 0.0),
                    'temperature' => (float)($row['temperature'] ?? 0.0),
                    'freq' => (int)($row['freq'] ?? 0),
                    'role' => (int)($row['role'] ?? 0),
                    'uptime' => (int)($row['uptime'] ?? 0),
                    'msgcntph' => (int)($row['msgcntph'] ?? 0),
                    'tracecntph' => (int)($row['tracecntph'] ?? 0),
                    'telemetrycntph' => (int)($row['telemetrycntph'] ?? 0),
                    'nodeinfocntph' => (int)($row['nodeinfocntph'] ?? 0),
                    'poscntph' => (int)($row['poscntph'] ?? 0),
                    'sumcntph' => (int)($row['sumcntph'] ?? 0),
                    'chutil' => (float)($row['chutil'] ?? 0.0),
					'lashchn' => (int)($row['lastchn'] ?? 0),
                    'is_stale' => $is_stale
                ];
                $nodes[] = $node_data;
                $nodesById[$node_data['node_id']] = $node_data;
            }

            // --- 3. Fetch SNR data (from map2.php) ---
            $snr_stmt = $db->query("SELECT node1, node2, snr FROM snr WHERE last_updated >= date('now', '-7 days') AND node1 != node2 AND snr != 0 AND node1 != -1 AND node2 != -1");
            $snrData = $snr_stmt->fetchAll(PDO::FETCH_ASSOC);

            // --- 4. Process data (logic from calculateAndShowStats) ---
            $filteredNodes = array_filter($nodes, function($node) use ($query_freq) {
                return $node['freq'] == $query_freq;
            });

            if (count($filteredNodes) === 0) {
                $response = [
                    'status' => 'success',
                    'frequency' => $query_freq,
                    'message' => "No node data to analyze for {$query_freq} MHz.",
                    'data' => ['recent_activity' => $formattedMainStats]
                ];
                break;
            }

            // Initialize counters
            $totalNodes = count($filteredNodes);
            $nodesWithCoords = 0;
            $staleNodes = 0;
            $onlineNodes = 0;
            
            $totalSumCntPh = 0;
            $totalMsgCntPh = 0;
            $totalTraceCntPh = 0;
            $totalTelemetryCntPh = 0;
            $totalNodeInfoCntPh = 0;
            $totalPosCntPh = 0;
            
            $sumBattery = 0;
            $countBatteryNodes = 0;
            $sumUptime = 0;
            $countUptimeNodes = 0;
            $sumChutil = 0;
            $countChutilNodes = 0;
            
            $roleCounts = [];

            $maxMsgCntPh = 0;
            $topMsgNodeName = 'N/A';
            $maxTraceCntPh = 0;
            $topTraceNodeName = 'N/A';
            $maxTelemetryCntPh = 0;
            $topTelemetryNodeName = 'N/A';
            $maxNodeInfoCntPh = 0;
            $topNodeInfoNodeName = 'N/A';
            $maxPosCntPh = 0;
            $topPosNodeName = 'N/A';
            $maxChutil = 0;
            $topChutilNodeName = 'N/A';

            foreach ($filteredNodes as $node) {
                if ($node['latitude'] != 0 || $node['longitude'] != 0) $nodesWithCoords++;
                if ($node['is_stale']) {
                    $staleNodes++;
                } else {
                    $onlineNodes++;
                    $totalSumCntPh += $node['sumcntph'];
                    $totalMsgCntPh += $node['msgcntph'];
                    $totalTraceCntPh += $node['tracecntph'];
                    $totalTelemetryCntPh += $node['telemetrycntph'];
                    $totalNodeInfoCntPh += $node['nodeinfocntph'];
                    $totalPosCntPh += $node['poscntph'];
                    
                    if ($node['battery_level'] > 0) {
                        $sumBattery += $node['battery_level'];
                        $countBatteryNodes++;
                    }
                    if ($node['uptime'] > 0) {
                        $sumUptime += $node['uptime'];
                        $countUptimeNodes++;
                    }
                    if ($node['chutil'] > 0) {
                        $sumChutil += $node['chutil'];
                        $countChutilNodes++;
                    }

                    $nodeDisplayName = ($node['short_name'] !== 'N/A' && !empty($node['short_name'])) ? $node['short_name'] : $node['long_name'];
                    
                    if ($node['msgcntph'] > $maxMsgCntPh) {
                        $maxMsgCntPh = $node['msgcntph'];
                        $topMsgNodeName = $nodeDisplayName;
                    }
                    if ($node['tracecntph'] > $maxTraceCntPh) {
                        $maxTraceCntPh = $node['tracecntph'];
                        $topTraceNodeName = $nodeDisplayName;
                    }
                    if ($node['telemetrycntph'] > $maxTelemetryCntPh) {
                        $maxTelemetryCntPh = $node['telemetrycntph'];
                        $topTelemetryNodeName = $nodeDisplayName;
                    }
                    if ($node['nodeinfocntph'] > $maxNodeInfoCntPh) {
                        $maxNodeInfoCntPh = $node['nodeinfocntph'];
                        $topNodeInfoNodeName = $nodeDisplayName;
                    }
                    if ($node['poscntph'] > $maxPosCntPh) {
                        $maxPosCntPh = $node['poscntph'];
                        $topPosNodeName = $nodeDisplayName;
                    }
                    if ($node['chutil'] > $maxChutil) {
                        $maxChutil = $node['chutil'];
                        $topChutilNodeName = $nodeDisplayName;
                    }
                }
                $roleCounts[$node['role']] = ($roleCounts[$node['role']] ?? 0) + 1;
            }

            // --- 5. Calculate averages ---
            $totalSumCntPs = $totalSumCntPh / 3600;
            $avgBatteryLevel = ($countBatteryNodes > 0) ? ($sumBattery / $countBatteryNodes) : 0;
            $avgUptimeSeconds = ($countUptimeNodes > 0) ? ($sumUptime / $countUptimeNodes) : 0;
            $avgUptimeString = formatUptimePhp($avgUptimeSeconds);
            $avgChutil = ($countChutilNodes > 0) ? ($sumChutil / $countChutilNodes) : 0;
            
            $avgRepeatCount = 0;
            if (isset($formattedMainStats[0]['all_packets']) && $formattedMainStats[0]['all_packets'] > 0 && $totalSumCntPh > 0) {
                 $avgRepeatCount = $formattedMainStats[0]['all_packets'] / $totalSumCntPh;
            }

            // --- 6. Process SNR data ---
            $filteredNodeIds = array_flip(array_map(function($n) { return $n['node_id']; }, $filteredNodes));
            
            $sumSnr = 0;
            $countSnr = 0;
            $minSnr = 999;
            $maxSnr = -999;
            $minSnrNode1Id = null;
            $minSnrNode2Id = null;
            $maxSnrNode1Id = null;
            $maxSnrNode2Id = null;
            
            $snrGood = 0;
            $snrOkay = 0;
            $snrWeak = 0;
            $snrBad = 0;
            
            $getNodeName = function($nodeId) use ($nodesById) {
                if (!isset($nodesById[$nodeId])) {
                    $hexId = dechex((int)$nodeId);
                    $shortHex = substr($hexId, -8);
                    return '!' . $shortHex;
                }
                $node = $nodesById[$nodeId];
                $name = ($node['short_name'] !== 'N/A' && !empty($node['short_name'])) ? $node['short_name'] : $node['long_name'];
                return ($name !== 'N/A' && !empty($name)) ? $name : $node['node_id_hex'];
            };

            if (count($snrData) > 0) {
                foreach ($snrData as $link) {
                    if (!isset($filteredNodeIds[$link['node1']]) || !isset($filteredNodeIds[$link['node2']])) {
                        continue;
                    }
                    
                    $snr = (float)$link['snr'];
                    if ($snr === 0.0) continue;

                    $sumSnr += $snr;
                    $countSnr++;

                    if ($snr < $minSnr) {
                        $minSnr = $snr;
                        $minSnrNode1Id = $link['node1'];
                        $minSnrNode2Id = $link['node2'];
                    }
                    if ($snr > $maxSnr) {
                        $maxSnr = $snr;
                        $maxSnrNode1Id = $link['node1'];
                        $maxSnrNode2Id = $link['node2'];
                    }

                    if ($snr > 0) $snrGood++;
                    else if ($snr >= -5) $snrOkay++;
                    else if ($snr >= -10) $snrWeak++;
                    else $snrBad++;
                }
            }

            $avgSnr = ($countSnr > 0) ? ($sumSnr / $countSnr) : 0;
            $bestLinkName = $maxSnrNode1Id ? ($getNodeName($maxSnrNode1Id) . ' → ' . $getNodeName($maxSnrNode2Id)) : 'N/A';
            $worstLinkName = $minSnrNode1Id ? ($getNodeName($minSnrNode1Id) . ' → ' . $getNodeName($minSnrNode2Id)) : 'N/A';

            // --- 7. Build final structured response ---
            $statsData = [
                'recent_activity' => $formattedMainStats,
                'node_status' => [
                    'total_nodes' => $totalNodes,
                    'online_nodes' => $onlineNodes,
                    'stale_nodes' => $staleNodes,
                    'nodes_with_gps' => $nodesWithCoords,
                ],
                'message_counts_per_hour' => [
                    'total_all_msgs' => (float)number_format($totalSumCntPh, 2, '.', ''),
                    'total_all_msgs_per_second' => (float)number_format($totalSumCntPs, 4, '.', ''),
                    'avg_repeat_count' => (float)number_format($avgRepeatCount, 2, '.', ''),
                    'text_msgs' => (int)$totalMsgCntPh,
                    'traceroute' => (int)$totalTraceCntPh,
                    'telemetry' => (int)$totalTelemetryCntPh,
                    'nodeinfo' => (int)$totalNodeInfoCntPh,
                    'position' => (int)$totalPosCntPh,
                ],
                'top_contributors_online' => [
                    'text_msgs' => ['node' => $topMsgNodeName, 'count' => (int)$maxMsgCntPh],
                    'traceroute' => ['node' => $topTraceNodeName, 'count' => (int)$maxTraceCntPh],
                    'telemetry' => ['node' => $topTelemetryNodeName, 'count' => (int)$maxTelemetryCntPh],
                    'nodeinfo' => ['node' => $topNodeInfoNodeName, 'count' => (int)$maxNodeInfoCntPh],
                    'position' => ['node' => $topPosNodeName, 'count' => (int)$maxPosCntPh],
                    'channel_utilization' => ['node' => $topChutilNodeName, 'percent' => (float)number_format($maxChutil, 1, '.', '')],
                ],
                'link_stats_snr_7_days' => [
                    'total_links_logged' => $countSnr,
                    'average_snr_db' => (float)number_format($avgSnr, 2, '.', ''),
                    'best_link_db' => (float)number_format($maxSnr, 2, '.', ''),
                    'best_link_nodes' => $bestLinkName,
                    'worst_link_db' => (float)number_format($minSnr, 2, '.', ''),
                    'worst_link_nodes' => $worstLinkName,
                    'distribution' => [
                        'good_gt_0db' => ['count' => $snrGood, 'percent' => (float)number_format($countSnr > 0 ? ($snrGood / $countSnr * 100) : 0, 1, '.', '')],
                        'okay_0_to_-5db' => ['count' => $snrOkay, 'percent' => (float)number_format($countSnr > 0 ? ($snrOkay / $countSnr * 100) : 0, 1, '.', '')],
                        'weak_-5_to_-10db' => ['count' => $snrWeak, 'percent' => (float)number_format($countSnr > 0 ? ($snrWeak / $countSnr * 100) : 0, 1, '.', '')],
                        'bad_lt_-10db' => ['count' => $snrBad, 'percent' => (float)number_format($countSnr > 0 ? ($snrBad / $countSnr * 100) : 0, 1, '.', '')],
                    ]
                ],
                'averages_online_nodes' => [
                    'battery_percent' => (float)number_format($avgBatteryLevel, 1, '.', ''),
                    'battery_node_count' => $countBatteryNodes,
                    'uptime_human' => $avgUptimeString,
                    'uptime_node_count' => $countUptimeNodes,
                    'channel_utilization_percent' => (float)number_format($avgChutil, 1, '.', ''),
                    'channel_utilization_node_count' => $countChutilNodes,
                ],
                'distribution_by_role' => []
            ];
            
            foreach ($roleCounts as $roleId => $count) {
                $statsData['distribution_by_role'][] = [
                    'role' => $ROLE_MAP_LONG[$roleId] ?? "Unknown ({$roleId})",
                    'count' => $count,
                    'percentage' => (float)number_format(($count / $totalNodes) * 100, 1, '.', '')
                ];
            }
            
            $response = ['status' => 'success', 'frequency' => $query_freq, 'data' => $statsData];
            break;

        /**
         * Actions: nodeinfo or snrinfo
         * Both require finding a node first.
         * Param: query (string) - Node short name (e.g., "MyNode") or hex ID (e.g., "!aabbccdd" or "aabbccdd")
         */
        case 'nodeinfo':
        case 'snrinfo':
            if (empty($query)) {
                $response['message'] = 'Missing query parameter (node hex ID or shortname).';
                break;
            }

            // Find the node by short name (case-insensitive) OR short hex ID (case-insensitive, with or without '!')
            $stmt = $db->prepare("
                SELECT * FROM nodes 
                WHERE UPPER(short_name) = UPPER(:query) 
                   OR UPPER('!' || SUBSTR(printf('%x', node_id), -8)) = UPPER(:query) 
                   OR UPPER(SUBSTR(printf('%x', node_id), -8)) = UPPER(:query)
            ");
            $stmt->execute([':query' => $query]);
            $node = $stmt->fetch(PDO::FETCH_ASSOC);

            if (!$node) {
                $response['message'] = "Node not found with identifier: " . htmlspecialchars($query);
                break;
            }

            // --- Handle nodeinfo action ---
            if ($action === 'nodeinfo') {
                $nodeData = [
                    'node_id_int' => (int)$node['node_id'],
                    'node_id_hex' => '!' . substr(sprintf('%x', $node['node_id']), -8),
                    'long_name' => $node['long_name'],
                    'short_name' => $node['short_name'],
                    'position' => [
                        'latitude' => $node['latitude'] / 10000000.0,
                        'longitude' => $node['longitude'] / 10000000.0
                    ],
                    'last_updated' => $node['last_updated'],
                    'frequency' => (int)$node['freq'],
                    'role' => $ROLE_MAP_LONG[$node['role']] ?? 'Unknown',
					'lastchn' => $node['lastchn'] ?? 0,
                    'uptime_seconds' => (int)$node['uptime'],
                    'telemetry' => [
                        'battery_level' => (int)$node['battery_level'],
                        'battery_voltage' => (float)$node['battery_voltage'],
                        'temperature' => (float)$node['temperature']
                    ],
                    'counts_per_hour' => [
                        'total' => (int)$node['sumcntph'],
                        'message' => (int)$node['msgcntph'],
                        'traceroute' => (int)$node['tracecntph'],
                        'telemetry' => (int)$node['telemetrycntph'],
                        'nodeinfo' => (int)$node['nodeinfocntph'],
                        'position' => (int)$node['poscntph']
                    ],
                    'channel_utilization_percent' => (float)$node['chutil']
                ];
                $response = ['status' => 'success', 'data' => $nodeData];
            } 
            
            // --- Handle snrinfo action ---
            else if ($action === 'snrinfo') {
                $node_id = $node['node_id'];
                $snrData = ['incoming' => [], 'outgoing' => []];

                // Get incoming SNR
                $stmt_in = $db->prepare("
                    SELECT T1.snr, T1.node1, T2.short_name, T2.long_name 
                    FROM snr AS T1 
                    LEFT JOIN nodes AS T2 ON T1.node1 = T2.node_id 
                    WHERE T1.node2 = :node_id 
                      AND T1.last_updated >= date('now', '-7 days') 
                      AND T1.snr != 0 AND T1.node1 != -1
                ");
                $stmt_in->execute([':node_id' => $node_id]);
                while ($row = $stmt_in->fetch(PDO::FETCH_ASSOC)) {
                    $hex = '!' . substr(sprintf('%x', $row['node1']), -8);
                    $name = ($row['short_name'] !== 'N/A' && !empty($row['short_name'])) ? $row['short_name'] : $hex;
                    $snrData['incoming'][] = [
                        'from_node_id' => (int)$row['node1'],
                        'from_node_name' => $name,
                        'snr' => (float)$row['snr']
                    ];
                }

                // Get outgoing SNR
                $stmt_out = $db->prepare("
                    SELECT T1.snr, T1.node2, T2.short_name, T2.long_name 
                    FROM snr AS T1 
                    LEFT JOIN nodes AS T2 ON T1.node2 = T2.node_id 
                    WHERE T1.node1 = :node_id 
                      AND T1.last_updated >= date('now', '-7 days') 
                      AND T1.snr != 0 AND T1.node2 != -1
                ");
                $stmt_out->execute([':node_id' => $node_id]);
                while ($row = $stmt_out->fetch(PDO::FETCH_ASSOC)) {
                    $hex = '!' . substr(sprintf('%x', $row['node2']), -8);
                    $name = ($row['short_name'] !== 'N/A' && !empty($row['short_name'])) ? $row['short_name'] : $hex;
                    $snrData['outgoing'][] = [
                        'to_node_id' => (int)$row['node2'],
                        'to_node_name' => $name,
                        'snr' => (float)$row['snr']
                    ];
                }
                
                $response = ['status' => 'success', 'data' => $snrData];
            }
            break;

        // Default case for unknown actions
        default:
            $response['message'] = 'Invalid action specified.';
            break;
    }

} catch (PDOException $e) {
    // Handle database errors
    $response['message'] = 'Database Error: ' . $e->getMessage();
}

// Final output
echo json_encode($response, JSON_PRETTY_PRINT);
exit;

/**
 * Helper function to format uptime seconds into a human-readable string.
 * Ported from the JavaScript in map2.php.
 * @param int $totalSeconds
 * @return string|null
 */
function formatUptimePhp($totalSeconds) {
    if (!$totalSeconds || $totalSeconds <= 0) return 'N/A';

    $days = floor($totalSeconds / 86400);
    $hours = floor(($totalSeconds % 86400) / 3600);
    $minutes = floor(($totalSeconds % 3600) / 60);
    
    if ($days > 0) {
        return "{$days}d {$hours}h";
    }
    if ($hours > 0) {
        return "{$hours}h {$minutes}m";
    }
    if ($minutes > 0) {
        return "{$minutes}m";
    }
    return floor($totalSeconds) . 's';
}
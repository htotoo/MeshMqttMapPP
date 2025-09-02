<?php

// --- NO CACHE HEADERS ---
header("Cache-Control: no-store, no-cache, must-revalidate, max-age=0");
header("Cache-Control: post-check=0, pre-check=0", false);
header("Pragma: no-cache");
header("Expires: 0");
// --- CONFIGURATION ---
$db_path = '/home/totoo/projects/meshlogger/build/nodes.db';

// --- DATABASE CONNECTION & DATA FETCHING ---
$nodes = [];
$snr_data = [];
$chat_messages = [];
$node_count = 0;

// --- Handle Sorting ---
$sort_by = $_GET['sort'] ?? 'last_updated'; // Default to last_updated
$order_by_sql = 'ORDER BY last_updated DESC'; // Default SQL
if ($sort_by === 'name') {
    $order_by_sql = 'ORDER BY long_name ASC';
}


try {
    // 1. Open the SQLite database
    $db = new PDO('sqlite:' . $db_path);
    $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    // Set a busy timeout of 5 seconds to handle concurrent access
    $db->setAttribute(PDO::ATTR_TIMEOUT, 5);

    // 2. Fetch all nodes and create a lookup map, using the selected order
    $node_map = [];
    // Updated query to fetch new fields and apply sorting
    $stmt = $db->query('SELECT node_id, short_name, long_name, latitude, longitude, last_updated, battery_level, temperature FROM nodes ' . $order_by_sql);
    $rows = $stmt->fetchAll(PDO::FETCH_ASSOC);
    foreach ($rows as $row) {
        $hex = sprintf('%x', $row['node_id']);
        $short_hex = substr($hex, -8);

        // Check if the node is stale (older than 1 day) using UTC time
        $is_stale = false;
        if (!empty($row['last_updated'])) {
            $utc = new DateTimeZone('UTC');
            $last_seen = new DateTime($row['last_updated'], $utc);
            $now = new DateTime('now', $utc); // Get current time in UTC
            $diff_seconds = $now->getTimestamp() - $last_seen->getTimestamp();
            if ($diff_seconds > 86400) { // 86400 seconds = 1 day
                $is_stale = true;
            }
        }


        $node_data = [
            'node_id' => $row['node_id'],
            'node_id_hex' => '!' . $short_hex,
            'short_name' => htmlspecialchars($row['short_name'] ?? 'N/A', ENT_QUOTES, 'UTF-8'),
            'long_name' => htmlspecialchars($row['long_name'] ?? 'N/A', ENT_QUOTES, 'UTF-8'),
            'latitude' => $row['latitude'] / 10000000.0,
            'longitude' => $row['longitude'] / 10000000.0,
            'last_updated' => $row['last_updated'], // Pass the raw timestamp
            'battery_level' => $row['battery_level'] ?? 0,
            'temperature' => $row['temperature'] ?? 0.0,
            'is_stale' => $is_stale // Add the stale flag
        ];
        $nodes[] = $node_data;
        $node_map[$row['node_id']] = $node_data; // Create map for easy lookup
    }
    $node_count = count($nodes);

    // 3. Fetch SNR data from the last 2 weeks
    $snr_stmt = $db->query("SELECT node1, node2, snr FROM snr WHERE last_updated >= date('now', '-14 days')");
    $snr_data = $snr_stmt->fetchAll(PDO::FETCH_ASSOC);


    // 4. Fetch chat messages from the last 5 days
    $chat_stmt = $db->query("SELECT node_id, message, timestamp, freq FROM chat WHERE timestamp >= date('now', '-5 days') ORDER BY timestamp DESC");
    $chat_rows = $chat_stmt->fetchAll(PDO::FETCH_ASSOC);
    foreach ($chat_rows as $chat_row) {
        $sender_id = $chat_row['node_id'];
		$freq = $chat_row['freq'];
        
        $sender_display = '!' . substr(sprintf('%x', $sender_id), -8); // Default to hex
        $has_coords = false;

        if (isset($node_map[$sender_id])) {
            $node_info = $node_map[$sender_id];
            // Use short_name if it's meaningful, otherwise use the pre-calculated hex ID
            $sender_display = ($node_info['short_name'] !== 'N/A') ? $node_info['short_name'] : $node_info['node_id_hex'];
            // Check for coordinates
            $has_coords = ($node_info['latitude'] != 0 || $node_info['longitude'] != 0);
        }

        $chat_messages[] = [
            'node_id' => $sender_id,
            'sender' => $sender_display,
            'message' => htmlspecialchars($chat_row['message'], ENT_QUOTES, 'UTF-8'),
            'timestamp' => htmlspecialchars($chat_row['timestamp'], ENT_QUOTES, 'UTF-8'),
			'freq' => $freq,
            'has_coords' => $has_coords
        ];
    }

} catch (PDOException $e) {
    // If there's an error, we display it and stop.
    die("Database Error: " . $e->getMessage());
}
?>
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Node Map</title>

    <!-- Leaflet CSS for the map -->
    <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" integrity="sha256-p4NxAoJBhIIN+hmNHrzRCf9tD/miZyoHS5obTRR9BMY=" crossorigin=""/>
    <!-- Leaflet.markercluster CSS -->
    <link rel="stylesheet" href="https://unpkg.com/leaflet.markercluster@1.4.1/dist/MarkerCluster.css" />
    <link rel="stylesheet" href="https://unpkg.com/leaflet.markercluster@1.4.1/dist/MarkerCluster.Default.css" />


    <style>
        body, html {
            margin: 0;
            padding: 0;
            height: 100%;
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
            overflow: hidden;
        }
        #main-container {
            display: flex;
            flex-direction: column;
            height: 100vh;
        }
        #top-section {
            display: flex;
            flex-grow: 1; /* Let this section grow to fill available space */
            transition: height 0.3s ease;
            overflow: hidden; /* Prevent content from spilling out during transition */
            position: relative; /* Needed for positioning the SNR toggle */
        }
        #map {
            flex: 1;
            height: 100%;
            background-color: #f0f0f0;
        }
        #snr-toggle-container {
            position: absolute;
            top: 10px;
            right: 10px;
            z-index: 1000;
            background-color: rgba(255, 255, 255, 0.8);
            padding: 8px;
            border-radius: 5px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.2);
            font-size: 14px;
        }
        #node-list-panel {
            width: 350px;
            background-color: #f8f9fa;
            display: flex;
            flex-direction: column;
            transition: width 0.3s ease, min-width 0.3s ease;
            min-width: 350px;
        }
        #node-list-panel.collapsed {
            width: 0;
            min-width: 0;
            overflow: hidden;
        }
        #panel-header {
            padding: 15px;
            background-color: #e9ecef;
            border-bottom: 1px solid #dee2e6;
            font-size: 1.2em;
            font-weight: bold;
            text-align: center;
            flex-shrink: 0;
        }
        #filter-container {
            padding: 10px;
            background-color: #e9ecef;
            border-bottom: 1px solid #dee2e6;
        }
        #node-filter-input {
            width: 100%;
            padding: 8px;
            box-sizing: border-box;
            border: 1px solid #ced4da;
            border-radius: .25rem;
        }
        #sort-container {
            padding: 8px 10px;
            font-size: 0.9em;
            text-align: right;
            background-color: #f8f9fa;
            border-bottom: 1px solid #dee2e6;
        }
        .sort-link {
            text-decoration: none;
            color: #007bff;
            padding: 2px 5px;
        }
        .sort-link.active {
            font-weight: bold;
            text-decoration: underline;
        }
        #panel-toggle-bar {
            width: 20px;
            background-color: #e9ecef;
            cursor: pointer;
            display: flex;
            align-items: center;
            justify-content: center;
            border-right: 1px solid #dee2e6;
            border-left: 1px solid #dee2e6;
        }
        #panel-toggle-bar:hover {
            background-color: #d3d9df;
        }
        #panel-toggle-btn {
            font-size: 1.5em;
            line-height: 1;
            user-select: none;
        }
        #node-list {
            overflow-y: auto;
            flex-grow: 1;
        }
        .node-item {
            padding: 12px 15px;
            border-bottom: 1px solid #e9ecef;
            cursor: pointer;
            transition: background-color 0.2s, opacity 0.3s;
        }
        .node-item:hover { background-color: #e2e6ea; }
        .node-item.no-coords { cursor: default; color: #6c757d; }
        .node-item.stale-node {
            opacity: 0.6;
        }
        .node-item b { font-size: 1.1em; color: #212529; }
        .node-item small { color: #495057; display: block; margin-top: 4px; }
        .node-id-hex { color: #007bff; font-family: monospace; }
        .node-stats {
            font-size: 0.8em;
            color: #6c757d;
            margin-top: 5px;
            display: flex;
            align-items: center;
            gap: 12px; /* Space between items */
        }
        .node-stats span {
            display: inline-flex;
            align-items: center;
        }

        #bottom-panel {
            flex-shrink: 0;
            height: 30%;
            background-color: #343a40;
            color: #f8f9fa;
            border-top: 2px solid #495057;
            display: flex;
            flex-direction: column;
            resize: vertical;
            overflow: auto;
            transition: height 0.3s ease;
        }
        #bottom-panel.collapsed {
            height: 40px; /* Height of the header */
            overflow: hidden;
        }
        #chat-header {
            padding: 10px 15px;
            background-color: #495057;
            font-weight: bold;
            display: flex;
            justify-content: space-between;
            align-items: center;
            cursor: pointer;
        }
        #chat-toggle-btn {
            font-size: 1.2em;
            user-select: none;
        }
        #chat-content {
            padding: 15px;
            overflow-y: auto;
            flex-grow: 1;
        }
        .chat-message {
            /* Spacing properties removed as requested */
        }
        .chat-timestamp {
            font-size: 0.8em;
            color: #adb5bd;
            margin-right: 10px;
        }
        .chat-sender {
            font-weight: bold;
            color: #17a2b8;
        }
        .chat-sender-link {
            font-weight: bold;
            color: #17a2b8;
            cursor: pointer;
            text-decoration: underline;
        }
        .chat-text {
            word-break: break-word;
        }
		
		.chat-title-link
		{
			color: white;
		}

        .snr-label {
            background-color: rgba(255, 255, 255, 0.7);
            border: none;
            border-radius: 3px;
            box-shadow: none;
            padding: 2px 5px;
            font-size: 12px;
            font-weight: bold;
            color: #333;
        }

        /* --- MOBILE RESPONSIVENESS --- */
        @media (max-width: 768px) {
            #node-list-panel {
                width: 260px; /* Narrower panel for mobile */
                min-width: 260px;
            }

            #node-list-panel.collapsed {
                width: 0;
                min-width: 0;
            }

            #panel-header {
                font-size: 1.1em; /* Slightly smaller header font */
            }
        }
    </style>
</head>
<body>

    <div id="main-container">
        <div id="top-section">
            <div id="node-list-panel">
                <div id="panel-header">
                    <span><a href="/">🏡</a></span><span>All Nodes (<?php echo $node_count; ?>)</span>
                </div>
                <div id="filter-container">
                    <input type="text" id="node-filter-input" placeholder="Filter by name or ID...">
                </div>
                <div id="sort-container">
                    Sort by:
                    <a href="?sort=last_updated" class="sort-link <?php if ($sort_by === 'last_updated') echo 'active'; ?>">Last Seen</a> |
                    <a href="?sort=name" class="sort-link <?php if ($sort_by === 'name') echo 'active'; ?>">Name</a>
                </div>
                <div id="node-list"></div>
            </div>
            <div id="panel-toggle-bar">
                <span id="panel-toggle-btn">&laquo;</span>
            </div>
            <div id="map"></div>
            <div id="snr-toggle-container">
                <div>
                  <input type="checkbox" id="snr-toggle" />
                  <label for="snr-toggle">Show SNR</label>
                </div>
                <div>
                  <input type="checkbox" id="hide-stale-toggle" />
                  <label for="hide-stale-toggle">Hide Stale Nodes</label>
                </div>
            </div>
        </div>
        <div id="bottom-panel">
            <div id="chat-header">
                <span>Chat / Log (<a href="https://t.me/+z2Yk9z31vDkyMzA8" onclick="event.stopPropagation();" class="chat-title-link" target="_blank" >Telegram csatorna</a>)</span>
                <span id="chat-toggle-btn">▼</span>
            </div>
            <div id="chat-content">
                <?php if (!empty($chat_messages)): ?>
                    <?php foreach ($chat_messages as $msg): ?>
                        <div class="chat-message">
                            <span class="chat-timestamp">[<?php echo $msg['timestamp']; ?>] [<?php echo $msg['freq']; ?>]</span>
                            <?php if ($msg['has_coords']): ?>
                                <a href="#" class="chat-sender-link" data-node-id="<?php echo $msg['node_id']; ?>"><?php echo $msg['sender']; ?>:</a>
                            <?php else: ?>
                                <span class="chat-sender"><?php echo $msg['sender']; ?>:</span>
                            <?php endif; ?>
                            <span class="chat-text"><?php echo $msg['message']; ?></span>
                        </div>
                    <?php endforeach; ?>
                <?php else: ?>
                    <p>No messages in the last 5 days.</p>
                <?php endif; ?>
            </div>
        </div>
    </div>

    <!-- Leaflet JavaScript library -->
    <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js" integrity="sha256-20nQCchB9co0qIjJZRGuk2/Z9VM+kNiyxNV1lvTlZBo=" crossorigin=""></script>
    <!-- Leaflet.markercluster JavaScript -->
    <script src="https://unpkg.com/leaflet.markercluster@1.4.1/dist/leaflet.markercluster.js"></script>

    <script>
        function timeAgo(timestamp) {
            if (!timestamp) return 'Soha';
            const ago = new Date(timestamp + ' UTC');
            const now = new Date();
            const diffSeconds = Math.round((now - ago) / 1000);

            if (diffSeconds < 0) return 'a jövőben';
            if (diffSeconds < 60) return `${diffSeconds} sec`;
            
            const diffMinutes = Math.round(diffSeconds / 60);
            if (diffMinutes < 60) return `${diffMinutes} min`;
            
            const diffHours = Math.round(diffMinutes / 60);
            if (diffHours < 24) return `${diffHours} hours`;
            
            const diffDays = Math.round(diffHours / 24);
            return `${diffDays} days`;
        }

        // --- DOM ELEMENTS ---
        const mapElement = document.getElementById('map');
        const nodeListPanel = document.getElementById('node-list-panel');
        const panelToggleBtn = document.getElementById('panel-toggle-btn');
        const nodeListElement = document.getElementById('node-list');
        const nodeFilterInput = document.getElementById('node-filter-input');
        const bottomPanel = document.getElementById('bottom-panel');
        const chatHeader = document.getElementById('chat-header');
        const chatToggleBtn = document.getElementById('chat-toggle-btn');
        const snrToggle = document.getElementById('snr-toggle');
        const hideStaleToggle = document.getElementById('hide-stale-toggle');

        // --- MAP INITIALIZATION ---
        const map = L.map(mapElement).setView([47.4979, 19.0402], 7);
        L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
            maxZoom: 19,
            attribution: '&copy; <a href="http://www.openstreetmap.org/copyright">OpenStreetMap</a>'
        }).addTo(map);

        // --- DATA & UI ---
        const nodes = <?php echo json_encode($nodes); ?>;
        const snrData = <?php echo json_encode($snr_data); ?>;
        const markerLayer = {};
        
        // --- CUSTOM MARKER ICONS ---
        const redIcon = new L.Icon({
            iconUrl: 'https://raw.githubusercontent.com/pointhi/leaflet-color-markers/master/img/marker-icon-2x-red.png',
            shadowUrl: 'https://cdnjs.cloudflare.com/ajax/libs/leaflet/0.7.7/images/marker-shadow.png',
            iconSize: [25, 41],
            iconAnchor: [12, 41],
            popupAnchor: [1, -34],
            shadowSize: [41, 41]
        });

        const blueIcon = new L.Icon({
            iconUrl: 'https://raw.githubusercontent.com/pointhi/leaflet-color-markers/master/img/marker-icon-2x-blue.png',
            shadowUrl: 'https://cdnjs.cloudflare.com/ajax/libs/leaflet/0.7.7/images/marker-shadow.png',
            iconSize: [25, 41],
            iconAnchor: [12, 41],
            popupAnchor: [1, -34],
            shadowSize: [41, 41]
        });


        // Create a MarkerClusterGroup with options
        const activeMarkersCluster = L.markerClusterGroup({
            maxClusterRadius: 20,
            disableClusteringAtZoom: 20
        });
        const staleMarkersCluster = L.markerClusterGroup({
            maxClusterRadius: 20,
            disableClusteringAtZoom: 20
        });
        
        // Layer for individual markers when clustering is off
        const activeIndividualMarkersLayer = L.layerGroup();
        const staleIndividualMarkersLayer = L.layerGroup();
        // Layer for SNR lines
        const snrLayer = L.layerGroup();


        // --- PANEL TOGGLE LOGIC ---
        panelToggleBtn.parentElement.addEventListener('click', () => {
            const isCollapsed = nodeListPanel.classList.toggle('collapsed');
            panelToggleBtn.innerHTML = isCollapsed ? '&raquo;' : '&laquo;';
            setTimeout(() => map.invalidateSize(), 300);
        });

        chatHeader.addEventListener('click', () => {
            const isCollapsed = bottomPanel.classList.toggle('collapsed');
            chatToggleBtn.innerHTML = isCollapsed ? '▲' : '▼';
            setTimeout(() => map.invalidateSize(), 300);
        });

        // --- Function to generate popup content dynamically ---
        function generatePopupContent(node) {
            let content = `<b>${node.long_name}</b> (${node.node_id_hex})<br>Short Name: ${node.short_name}<br>Last Seen: ${timeAgo(node.last_updated)}`;
            if (node.battery_level > 0) {
                content += `<br>🔋 ${node.battery_level}%`;
            }
            const temp = parseFloat(node.temperature);
            if (temp !== 0) {
                content += `<br>🌡️ ${temp.toFixed(1)}°C`;
            }

            if (snrToggle.checked) {
                const nodesById = Object.fromEntries(nodes.map(n => [n.node_id, n]));
                const snrTo = [];
                const snrFrom = [];

                snrData.forEach(link => {
                    // SNR to this node
                    if (link.node2 === node.node_id) {
                        const sourceNode = nodesById[link.node1];
                        if (sourceNode) {
                            snrTo.push(`${sourceNode.short_name} (${sourceNode.node_id_hex}): ${link.snr} dB`);
                        }
                    }
                    // SNR from this node
                    if (link.node1 === node.node_id) {
                        const destNode = nodesById[link.node2];
                        if (destNode) {
                            snrFrom.push(`${destNode.short_name} (${destNode.node_id_hex}): ${link.snr} dB`);
                        }
                    }
                });

                if (snrTo.length > 0) {
                    content += `<br><br><b>SNR to this node:</b><br>${snrTo.join('<br>')}`;
                }
                if (snrFrom.length > 0) {
                    content += `<br><br><b>SNR from this node:</b><br>${snrFrom.join('<br>')}`;
                }
            }
            return content;
        }

        // --- Create Map Markers ---
        nodes.forEach(node => {
            if (node.latitude !== 0 || node.longitude !== 0) {
                const iconToUse = node.is_stale ? redIcon : blueIcon;
                const popupFunction = () => generatePopupContent(node);

                const marker = L.marker([node.latitude, node.longitude], { icon: iconToUse });
                marker.bindPopup(popupFunction);
                
                markerLayer[node.node_id] = marker;
                
                const individualMarker = L.marker([node.latitude, node.longitude], { icon: iconToUse });
                individualMarker.node_id = node.node_id; // Add unique ID to marker
                individualMarker.bindPopup(popupFunction);

                if (node.is_stale) {
                    staleMarkersCluster.addLayer(marker);
                    staleIndividualMarkersLayer.addLayer(individualMarker);
                } else {
                    activeMarkersCluster.addLayer(marker);
                    activeIndividualMarkersLayer.addLayer(individualMarker);
                }
            }
        });
        
        map.addLayer(activeMarkersCluster);
        map.addLayer(staleMarkersCluster);

        // --- Populate Node List Panel ---
        if (nodes.length > 0) {
            nodes.forEach(node => {
                const nodeItem = document.createElement('div');
                const hasCoords = node.latitude !== 0 || node.longitude !== 0;
                nodeItem.className = hasCoords ? 'node-item' : 'node-item no-coords';
                if (node.is_stale) {
                    nodeItem.classList.add('stale-node');
                }
                
                nodeItem.dataset.filterText = `${node.long_name} ${node.short_name} ${node.node_id_hex}`.toLowerCase();
                
                let statsHtml = '';
                if (node.battery_level > 0) {
                    statsHtml += `<span>🔋 ${node.battery_level}%</span>`;
                }
                const temp = parseFloat(node.temperature);
                if (temp !== 0) {
                    statsHtml += `<span>🌡️ ${temp.toFixed(1)}°C</span>`;
                }

                nodeItem.innerHTML = `
                    <div>
                        <b>${node.long_name}</b> 
                        (${node.short_name} <span class="node-id-hex">${node.node_id_hex}</span>)
                        <small>Last Seen: ${timeAgo(node.last_updated)}</small>
                    </div>
                    ${statsHtml ? `<div class="node-stats">${statsHtml}</div>` : ''}
                `;

                if (hasCoords) {
                    nodeItem.addEventListener('click', () => {
                        const marker = markerLayer[node.node_id];
                        if (marker) {
                            const isSnrActive = snrToggle.checked;
                            map.setView([node.latitude, node.longitude], isSnrActive ? 15 : 13);

                            if (isSnrActive) {
                                const targetLayer = node.is_stale ? staleIndividualMarkersLayer : activeIndividualMarkersLayer;
                                targetLayer.eachLayer(indMarker => {
                                    if (indMarker.node_id === node.node_id) {
                                        indMarker.openPopup();
                                    }
                                });
                            } else {
                                const targetCluster = node.is_stale ? staleMarkersCluster : activeMarkersCluster;
                                targetCluster.zoomToShowLayer(marker, function() {
    							    marker.openPopup();
    							});
                            }
                        }
                    });
                }
                nodeListElement.appendChild(nodeItem);
            });
            
            if (Object.keys(markerLayer).length > 0) {
                 const allMarkersGroup = L.featureGroup([activeMarkersCluster, staleMarkersCluster]);
                map.fitBounds(allMarkersGroup.getBounds().pad(0.2));
            }

        } else {
            nodeListElement.innerHTML = '<div style="padding: 15px; text-align: center; color: #6c757d;">No nodes found.</div>';
        }

        // --- NODE LIST FILTER & VISIBILITY LOGIC ---
        function updateNodeListVisibility() {
            const filterValue = nodeFilterInput.value.toLowerCase();
            const hideStale = hideStaleToggle.checked;
            const allNodeItems = nodeListElement.querySelectorAll('.node-item');

            allNodeItems.forEach(item => {
                const isStale = item.classList.contains('stale-node');
                const matchesFilter = item.dataset.filterText.includes(filterValue);

                if (matchesFilter && (!hideStale || !isStale)) {
                    item.style.display = '';
                } else {
                    item.style.display = 'none';
                }
            });
        }
        nodeFilterInput.addEventListener('keyup', updateNodeListVisibility);
        hideStaleToggle.addEventListener('change', updateNodeListVisibility);


        // --- CHAT SENDER CLICK LOGIC ---
        document.querySelectorAll('.chat-sender-link').forEach(link => {
            link.addEventListener('click', (event) => {
                event.preventDefault();
                const nodeId = link.getAttribute('data-node-id');
                const marker = markerLayer[nodeId];
                const node = nodes.find(n => n.node_id == nodeId);

                if (marker && node) {
                    const isSnrActive = snrToggle.checked;
                    map.setView([node.latitude, node.longitude], isSnrActive ? 15 : 13);
                    if (!isSnrActive) {
                        const targetCluster = node.is_stale ? staleMarkersCluster : activeMarkersCluster;
                        targetCluster.zoomToShowLayer(marker, function() {
                            marker.openPopup();
                        });
                    } else {
                        const targetLayer = node.is_stale ? staleIndividualMarkersLayer : activeIndividualMarkersLayer;
                        targetLayer.eachLayer(indMarker => {
                            if (indMarker.node_id == nodeId) {
                                indMarker.openPopup();
                            }
                        });
                    }
                }
            });
        });

        // --- SNR TOGGLE LOGIC ---
        function getSnrColor(snr) {
            // Clamp SNR between -20 and 0 for color calculation
            const clampedSnr = Math.max(-20, Math.min(snr, 0));
            // Calculate percentage from -20 (0%) to 0 (100%)
            const percentage = (clampedSnr + 20) / 20;

            // Interpolate between red (0%) and green (100%)
            const red = 255 * (1 - percentage);
            const green = 255 * percentage;
            const blue = 0;

            return `rgb(${Math.round(red)}, ${Math.round(green)}, ${Math.round(blue)})`;
        }

        function drawSnrLines() {
            snrLayer.clearLayers();
            const processedPairs = new Set();
            const nodesById = Object.fromEntries(nodes.map(n => [n.node_id, n]));

            snrData.forEach(link => {
                const pairKeyFwd = `${link.node1}-${link.node2}`;
                const pairKeyRev = `${link.node2}-${link.node1}`;

                if (processedPairs.has(pairKeyFwd) || processedPairs.has(pairKeyRev)) return;

                const node1 = nodesById[link.node1];
                const node2 = nodesById[link.node2];

                if (!node1 || !node2 || (node1.latitude === 0 && node1.longitude === 0) || (node2.latitude === 0 && node2.longitude === 0)) return;

                const pos1 = L.latLng(node1.latitude, node1.longitude);
                const pos2 = L.latLng(node2.latitude, node2.longitude);

                const reverseLink = snrData.find(r => r.node1 == link.node2 && r.node2 == link.node1);

                if (reverseLink) {
                    // Bidirectional link, draw two curved lines
                    processedPairs.add(pairKeyFwd);
                    processedPairs.add(pairKeyRev);

                    const p1 = map.latLngToContainerPoint(pos1);
                    const p2 = map.latLngToContainerPoint(pos2);
                    
                    const distance = p1.distanceTo(p2);
                    const offset = Math.min(20, distance * 0.15); // Dynamic offset

                    const angle = Math.atan2(p2.y - p1.y, p2.x - p1.x);
                    const offsetX = offset * Math.sin(angle);
                    const offsetY = -offset * Math.cos(angle);

                    const midPoint1 = map.containerPointToLatLng([ (p1.x + p2.x)/2 + offsetX, (p1.y + p2.y)/2 + offsetY ]);
                    const midPoint2 = map.containerPointToLatLng([ (p1.x + p2.x)/2 - offsetX, (p1.y + p2.y)/2 - offsetY ]);
                    
                    const line1 = L.polyline([pos1, midPoint1, pos2], { color: getSnrColor(link.snr), weight: 3 });
                    line1.bindTooltip(`${link.snr}`, { permanent: true, direction: 'center', className: 'snr-label' });
                    snrLayer.addLayer(line1);
                    
                    const line2 = L.polyline([pos1, midPoint2, pos2], { color: getSnrColor(reverseLink.snr), weight: 3 });
                    line2.bindTooltip(`${reverseLink.snr}`, { permanent: true, direction: 'center', className: 'snr-label' });
                    snrLayer.addLayer(line2);

                } else {
                    // Unidirectional link, draw a straight line
                    const line = L.polyline([pos1, pos2], { color: getSnrColor(link.snr), weight: 3 });
                    line.bindTooltip(`${link.snr}`, { permanent: true, direction: 'center', className: 'snr-label' });
                    snrLayer.addLayer(line);
                }
            });
        }

        function updateMapView() {
            const showSnr = snrToggle.checked;
            const hideStale = hideStaleToggle.checked;

            // --- Remove all possible layers first ---
            [activeMarkersCluster, staleMarkersCluster, activeIndividualMarkersLayer, staleIndividualMarkersLayer, snrLayer].forEach(layer => {
                if (map.hasLayer(layer)) {
                    map.removeLayer(layer);
                }
            });

            if (showSnr) {
                // --- Add individual marker layers ---
                map.addLayer(activeIndividualMarkersLayer);
                if (!hideStale) {
                    map.addLayer(staleIndividualMarkersLayer);
                }
                // --- Add SNR lines ---
                drawSnrLines();
                map.addLayer(snrLayer);
            } else {
                // --- Add cluster layers ---
                map.addLayer(activeMarkersCluster);
                 if (!hideStale) {
                    map.addLayer(staleMarkersCluster);
                }
            }
        }


        snrToggle.addEventListener('change', updateMapView);
        hideStaleToggle.addEventListener('change', updateMapView);


        // Redraw lines on zoom to adjust curves
        map.on('zoomend', function() {
            if (snrToggle.checked) {
                drawSnrLines();
            }
        });

    </script>

</body>
</html>


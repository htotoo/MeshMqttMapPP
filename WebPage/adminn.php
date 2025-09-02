<?php

// --- KONFIGURÁCIÓ ---
// Az adatbázis elérési útja. Ugyanazt használja, mint a map.php.
$db_path = '/home/totoo/projects/meshlogger/build/nodes.db';
$username = 'admin';
$password = 'aaaaaa';

// --- BASIC AUTHENTIKÁCIÓ ---
if (
    !isset($_SERVER['PHP_AUTH_USER']) ||
    !isset($_SERVER['PHP_AUTH_PW']) ||
    $_SERVER['PHP_AUTH_USER'] !== $username ||
    $_SERVER['PHP_AUTH_PW'] !== $password
) {
    header('WWW-Authenticate: Basic realm="Admin Area"');
    header('HTTP/1.0 401 Unauthorized');
    echo 'Hozzáférés megtagadva. A folytatáshoz érvényes felhasználónév és jelszó szükséges.';
    exit;
}

// --- TÖRLÉSI MŰVELETEK KEZELÉSE ---
$feedback_message = '';
$error = false;

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    try {
        // Adatbázis kapcsolat létrehozása
        $db = new PDO('sqlite:' . $db_path);
        $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);

        // Régi node-ok törlése
        if (isset($_POST['delete_old_nodes'])) {
            // Az SQLite datetime függvényét használjuk a 14 napnál régebbi rekordok kiválasztására
            $stmt = $db->prepare("DELETE FROM nodes WHERE last_updated < datetime('now', '-14 days')");
            $stmt->execute();
            $deleted_count = $stmt->rowCount(); // Megszámoljuk a törölt sorokat
            $feedback_message = "Sikeres törlés. Eltávolított node-ok száma: {$deleted_count}.";
        }

        // Régi chat üzenetek törlése
        if (isset($_POST['delete_old_chats'])) {
            $stmt = $db->prepare("DELETE FROM chat WHERE timestamp < datetime('now', '-14 days')");
            $stmt->execute();
            $deleted_count = $stmt->rowCount();
            $feedback_message = "Sikeres törlés. Eltávolított chat üzenetek száma: {$deleted_count}.";
        }
    } catch (PDOException $e) {
        // Hiba esetén üzenetet küldünk
        $feedback_message = "Adatbázis hiba történt: " . $e->getMessage();
        $error = true;
    }
}
?>
<!DOCTYPE html>
<html lang="hu">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Adminisztráció</title>
    <style>
        body { 
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif; 
            line-height: 1.6; 
            padding: 20px; 
            background-color: #f4f4f4; 
            color: #333;
        }
        .container { 
            max-width: 700px; 
            margin: auto; 
            background: #fff; 
            padding: 25px; 
            border-radius: 8px; 
            box-shadow: 0 4px 8px rgba(0,0,0,0.1); 
        }
        h1 { 
            color: #212529;
            border-bottom: 2px solid #e9ecef;
            padding-bottom: 10px;
        }
        .task { 
            border: 1px solid #dee2e6; 
            padding: 20px; 
            margin-top: 20px; 
            border-radius: 5px; 
            background-color: #f8f9fa;
        }
        p { 
            color: #495057; 
            margin-top: 0;
        }
        button { 
            background-color: #dc3545; 
            color: white; 
            padding: 12px 20px; 
            border: none; 
            border-radius: 5px; 
            cursor: pointer; 
            font-size: 16px; 
            transition: background-color 0.2s;
        }
        button:hover { 
            background-color: #c82333; 
        }
        .feedback { 
            padding: 15px; 
            margin-bottom: 20px; 
            border-radius: 5px; 
            color: #fff;
            text-align: center;
        }
        .feedback.success { 
            background-color: #28a745; 
        }
        .feedback.error { 
            background-color: #dc3545; 
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Adminisztrációs felület</h1>

        <?php if (!empty($feedback_message)): ?>
            <div class="feedback <?php echo $error ? 'error' : 'success'; ?>">
                <?php echo htmlspecialchars($feedback_message); ?>
            </div>
        <?php endif; ?>

        <div class="task">
            <h2>Régi node-ok törlése</h2>
            <p>Ezzel a gombbal törölheti azokat a node-okat, amelyek több mint 14 napja nem küldtek adatot.</p>
            <form method="POST" onsubmit="return confirm('Biztosan törölni szeretné a 14 napnál régebbi node-okat? A művelet nem vonható vissza!');">
                <button type="submit" name="delete_old_nodes">Régi nodeinfo-k törlése</button>
            </form>
        </div>

        <div class="task">
            <h2>Régi chat üzenetek törlése</h2>
            <p>Ezzel a gombbal törölheti a 14 napnál régebbi chat üzeneteket az adatbázisból.</p>
            <form method="POST" onsubmit="return confirm('Biztosan törölni szeretné a 14 napnál régebbi chat üzeneteket? A művelet nem vonható vissza!');">
                <button type="submit" name="delete_old_chats">Régi chatek törlése</button>
            </form>
        </div>
    </div>
</body>
</html>
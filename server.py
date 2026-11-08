from flask import Flask, request, jsonify, send_file
import time
import threading
import random
import string

app = Flask(__name__)

# Persistent network pool - these networks persist across scans
network_pool = []

def init_network_pool():
    """Initialize a pool of persistent networks"""
    global network_pool
    network_pool = []
    # Create 8-12 persistent networks
    for i in range(random.randint(8, 12)):
        suffix = ''.join(random.choices(string.hexdigits[:16].upper(), k=4))
        band = random.choice(['2G', '5G'])
        ssid = f'TP-Link_{band}_{suffix}'
        base_rssi = random.randint(-80, -40)
        auth = random.choice([0, 3, 4, 4, 4])
        network_pool.append({
            'ssid': ssid,
            'base_rssi': base_rssi,
            'auth': auth,
            'visible': True  # Networks can be temporarily hidden
        })

def generate_networks():
    """Generate network scan results with fluctuating signals"""
    # Initialize pool if empty
    if not network_pool:
        init_network_pool()

    # Randomly toggle visibility of some networks (10% chance)
    for network in network_pool:
        if random.random() < 0.1:
            network['visible'] = not network['visible']

    # Ensure at least 3 networks are visible
    visible_count = sum(1 for n in network_pool if n['visible'])
    if visible_count < 3:
        hidden = [n for n in network_pool if not n['visible']]
        for n in hidden[:3 - visible_count]:
            n['visible'] = True

    # Generate results from visible networks with signal fluctuation
    results = []
    for network in network_pool:
        if network['visible']:
            # Fluctuate signal by ±8 dBm
            rssi = network['base_rssi'] + random.randint(-8, 8)
            results.append({
                'ssid': network['ssid'],
                'rssi': rssi,
                'auth': network['auth']
            })

    return results

saved_networks = [
    {'ssid': 'HomeNetwork', 'password': 'inspirare'},
    {'ssid': 'Office_5G', 'password': 'work123'}
]

@app.route('/scan')
def get_networks():
    """Simulate network scanning with 2s delay"""
    time.sleep(2)
    # Generate fresh random networks each scan
    networks = generate_networks()
    # Return in the format expected by the HTML
    return jsonify({
        'support_5g': True,
        'aps': networks
    })

@app.route('/saved/list')
def get_saved():
    """Return list of saved network SSIDs"""
    return jsonify([n['ssid'] for n in saved_networks])

@app.route('/saved/set_default')
def set_default():
    """Move network at index to front of list"""
    index = request.args.get('index', type=int)
    if index is not None and 0 <= index < len(saved_networks):
        network = saved_networks.pop(index)
        saved_networks.insert(0, network)
        return jsonify({})
    else:
        return jsonify({'error': 'Invalid index'}), 400

@app.route('/saved/delete')
def delete_saved():
    """Remove network at index from saved list"""
    index = request.args.get('index', type=int)
    if index is not None and 0 <= index < len(saved_networks):
        saved_networks.pop(index)
        return jsonify({})
    else:
        return jsonify({'error': 'Invalid index'}), 400

@app.route('/submit', methods=['POST'])
def submit():
    """Attempt to connect to network"""
    data = request.get_json()
    ssid = data.get('ssid')
    password = data.get('password')

    print(f'Attempting to connect to: {ssid}')

    # Simulate 3 second connection attempt
    time.sleep(3)

    # Special behavior: only "inspirare" password works
    if password == 'inspirare':
        print('✓ Connection successful')
        return jsonify({'success': True})
    else:
        print('✗ Connection failed - wrong password')
        return jsonify({
            'success': False,
            'error': 'Connection failed. Wrong password.'
        }), 401

@app.route('/exit', methods=['POST'])
def exit_config():
    """Exit configuration mode and shutdown server"""
    print('Exit requested')

    # Shutdown server after response
    def shutdown():
        time.sleep(0.5)
        print('Server shutting down...')
        func = request.environ.get('werkzeug.server.shutdown')
        if func:
            func()

    threading.Thread(target=shutdown).start()
    return jsonify({'success': True})

@app.route('/')
def index():
    """Serve the HTML configuration page"""
    return send_file('assets/wifi_configuration.html')

if __name__ == '__main__':
    print('Mock WiFi backend running at http://localhost:9876')
    print('Test credentials:')
    print('  Network: any TP-Link network from the scanned list')
    print('  Password: "inspirare" (works) or anything else (fails)')
    app.run(port=9876, debug=False)

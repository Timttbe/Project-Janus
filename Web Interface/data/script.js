// Estado global da aplicação
let relays = [];
let syncEnabled = true;
let refreshInterval = null;

// Inicialização
document.addEventListener('DOMContentLoaded', function() {
    loadRelaysFromStorage();
    setupEventListeners();
    startPeriodicRefresh();
    logToTerminal('Sistema iniciado - Interface de controle carregada');
});

// Configurar event listeners
function setupEventListeners() {
    document.getElementById('syncToggle').addEventListener('change', function() {
        syncEnabled = this.checked;
        document.getElementById('syncStatus').textContent = syncEnabled ? 'Ativada' : 'Desativada';
        logToTerminal(`Sincronização ${syncEnabled ? 'ativada' : 'desativada'}`);
        saveToStorage();
    });
}

// Adicionar novo relay
function addRelay() {
    const name = document.getElementById('relayName').value.trim();
    const ip = document.getElementById('relayIP').value.trim();

    if (!name || !ip) {
        alert('Por favor, preencha o nome e IP do relay');
        return;
    }

    if (!isValidIP(ip)) {
        alert('Por favor, insira um IP válido');
        return;
    }

    if (relays.find(r => r.ip === ip)) {
        alert('Este IP já está cadastrado');
        return;
    }

    const relay = {
        id: Date.now(),
        name: name,
        ip: ip,
        status: 'unknown',
        relayState: 'unknown',
        online: false,
        lastUpdate: new Date()
    };

    relays.push(relay);
    renderRelayGrid();
    saveToStorage();
    
    // Limpar campos
    document.getElementById('relayName').value = '';
    document.getElementById('relayIP').value = '';

    logToTerminal(`Relay adicionado: ${name} (${ip})`);
    
    // Testar conexão imediatamente
    checkRelayStatus(relay);
}

// Validar IP
function isValidIP(ip) {
    const ipRegex = /^(\d{1,3}\.){3}\d{1,3}$/;
    if (!ipRegex.test(ip)) return false;
    
    const parts = ip.split('.');
    return parts.every(part => parseInt(part) >= 0 && parseInt(part) <= 255);
}

// Renderizar grid de relays
function renderRelayGrid() {
    const grid = document.getElementById('relayGrid');
    
    if (relays.length === 0) {
        grid.innerHTML = `
            <div style="text-align: center; padding: 40px; color: #666; grid-column: 1/-1;">
                <h3>Nenhum relay cadastrado</h3>
                <p>Adicione um relay usando o formulário acima</p>
            </div>
        `;
        return;
    }

    grid.innerHTML = relays.map(relay => `
        <div class="relay-card ${relay.online ? 'online' : 'offline'}" id="relay-${relay.id}">
            <div class="relay-header">
                <div class="relay-name">${relay.name}</div>
                <div class="status-indicator ${relay.online ? 'online' : 'offline'}"></div>
            </div>
            
            <div class="relay-info">
                <div><strong>IP:</strong> ${relay.ip}</div>
                <div><strong>Status:</strong> <span id="status-${relay.id}">${relay.online ? 'Online' : 'Offline'}</span></div>
                <div><strong>Relay:</strong> <span id="relay-state-${relay.id}" style="color: ${relay.relayState === 'on' ? '#4CAF50' : relay.relayState === 'off' ? '#f44336' : '#666'};">${relay.relayState.toUpperCase()}</span></div>
                ${relay.peerConnected !== undefined ? `<div><strong>Peer:</strong> <span style="color: ${relay.peerConnected ? '#4CAF50' : '#f44336'};">${relay.peerConnected ? 'Conectado' : 'Desconectado'}</span></div>` : ''}
                ${relay.uptime ? `<div><strong>Uptime:</strong> ${Math.floor(relay.uptime/60)}min ${relay.uptime%60}s</div>` : ''}
                ${relay.signalStrength ? `<div><strong>Sinal:</strong> ${relay.signalStrength}dBm</div>` : ''}
                <div><strong>Última atualização:</strong> <span id="last-update-${relay.id}">${formatTime(relay.lastUpdate)}</span></div>
            </div>
            
            <div class="relay-controls">
                <button class="btn ${relay.relayState === 'on' ? 'btn-warning' : 'btn-primary'}" 
                        onclick="controlRelay('${relay.id}', 'ON')" 
                        ${!relay.online ? 'disabled' : ''}>
                    ${relay.relayState === 'on' ? '🟡 Já Ligado' : '🟢 Ligar'}
                </button>
                <button class="btn ${relay.relayState === 'off' ? 'btn-warning' : 'btn-danger'}" 
                        onclick="controlRelay('${relay.id}', 'OFF')" 
                        ${!relay.online ? 'disabled' : ''}>
                    ${relay.relayState === 'off' ? '🟡 Já Desligado' : '🔴 Desligar'}
                </button>
                <button class="btn btn-info" onclick="toggleRelay('${relay.id}')" 
                        ${!relay.online ? 'disabled' : ''}
                        style="grid-column: 1/3;">
                    🔄 Alternar Estado
                </button>
                <button class="btn btn-info" onclick="checkRelayStatus(${JSON.stringify(relay).replace(/"/g, '&quot;')})" 
                        style="grid-column: 1/2;">
                    📊 Atualizar
                </button>
                <button class="btn btn-warning" onclick="removeRelay('${relay.id}')" 
                        style="grid-column: 2/3;">
                    🗑️ Remover
                </button>
            </div>
        </div>
    `).join('');
}

// Controlar relay individual
async function controlRelay(relayId, action) {
    const relay = relays.find(r => r.id == relayId);
    if (!relay) return;

    try {
        showLoading(relayId);
        logToTerminal(`Enviando comando ${action} para ${relay.name}...`);

        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), 8000);

        const response = await fetch(`http://${relay.ip}/${action}`, {
            method: 'GET',
            signal: controller.signal,
            mode: 'cors'
        });

        clearTimeout(timeoutId);

        if (response.ok) {
            const contentType = response.headers.get('content-type');
            let result;
            
            if (contentType && contentType.includes('application/json')) {
                result = await response.json();
                relay.relayState = result.relay_state ? result.relay_state.toLowerCase() : action.toLowerCase();
                relay.peerConnected = result.peer_connected || false;
                relay.uptime = result.uptime || 0;
                
                logToTerminal(`✅ ${relay.name}: ${result.message || result.status}`);
                
                if (result.peer_connected) {
                    logToTerminal(`🔗 ${relay.name} conectado ao peer`);
                }
            } else {
                result = await response.text();
                relay.relayState = action.toLowerCase();
                logToTerminal(`✅ ${relay.name}: ${result}`);
            }

            relay.online = true;
            relay.lastUpdate = new Date();

            // Sincronizar com outros relays se habilitado
            if (syncEnabled && relays.length > 1) {
                await syncOtherRelays(relayId, action);
            }
        } else {
            throw new Error(`HTTP ${response.status} - ${response.statusText}`);
        }
    } catch (error) {
        relay.online = false;
        if (error.name === 'AbortError') {
            logToTerminal(`⏱️ Timeout ao controlar ${relay.name}`);
        } else {
            logToTerminal(`❌ Erro ao controlar ${relay.name}: ${error.message}`);
        }
    } finally {
        hideLoading(relayId);
        renderRelayGrid();
        saveToStorage();
    }
}

// Alternar estado do relay
async function toggleRelay(relayId) {
    const relay = relays.find(r => r.id == relayId);
    if (!relay || !relay.online) return;

    const newAction = relay.relayState === 'on' ? 'OFF' : 'ON';
    await controlRelay(relayId, newAction);
}

// Sincronizar outros relays
async function syncOtherRelays(excludeId, action) {
    const otherRelays = relays.filter(r => r.id != excludeId && r.online);
    
    if (otherRelays.length === 0) {
        logToTerminal('🔄 Nenhum outro relay online para sincronizar');
        return;
    }

    logToTerminal(`🔄 Sincronizando ${action} com ${otherRelays.length} relays...`);
    
    for (const relay of otherRelays) {
        try {
            const controller = new AbortController();
            const timeoutId = setTimeout(() => controller.abort(), 3000);

            const response = await fetch(`http://${relay.ip}/${action}_SYNC`, {
                method: 'GET',
                signal: controller.signal,
                mode: 'cors'
            });

            clearTimeout(timeoutId);

            if (response.ok) {
                relay.relayState = action.toLowerCase();
                relay.lastUpdate = new Date();
                logToTerminal(`✅ Sincronizado: ${relay.name} → ${action}`);
            } else {
                throw new Error(`HTTP ${response.status}`);
            }
        } catch (error) {
            if (error.name === 'AbortError') {
                logToTerminal(`⏱️ Timeout na sincronização: ${relay.name}`);
            } else {
                logToTerminal(`⚠️ Falha na sincronização: ${relay.name} - ${error.message}`);
            }
        }
    }
    
    renderRelayGrid();
}

// Verificar status de um relay
async function checkRelayStatus(relay) {
    try {
        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), 5000);

        const response = await fetch(`http://${relay.ip}/info`, {
            method: 'GET',
            signal: controller.signal,
            mode: 'cors'
        });

        clearTimeout(timeoutId);

        if (response.ok) {
            const contentType = response.headers.get('content-type');
            let result;
            
            if (contentType && contentType.includes('application/json')) {
                result = await response.json();
                relay.online = true;
                relay.relayState = result.relay_state ? result.relay_state.toLowerCase() : 'unknown';
                relay.peerConnected = result.peer_connected || false;
                relay.uptime = result.uptime_seconds || 0;
                relay.signalStrength = result.signal_strength || 0;
                relay.freeHeap = result.free_heap || 0;
                relay.lastUpdate = new Date();
                
                logToTerminal(`📊 ${relay.name}: ${result.relay_state}, Peer: ${result.peer_connected ? 'Conectado' : 'Desconectado'}, Uptime: ${Math.floor(result.uptime_seconds/60)}min`);
            } else {
                // Fallback para resposta de texto simples
                result = await response.text();
                relay.online = true;
                relay.relayState = result.includes('ON') ? 'on' : 'off';
                relay.lastUpdate = new Date();
                
                logToTerminal(`📊 Status ${relay.name}: ${result}`);
            }
        } else {
            throw new Error(`HTTP ${response.status} - ${response.statusText}`);
        }
    } catch (error) {
        relay.online = false;
        relay.relayState = 'unknown';
        
        if (error.name === 'AbortError') {
            logToTerminal(`⏱️ Timeout ao verificar ${relay.name}`);
        } else {
            logToTerminal(`❌ ${relay.name} offline: ${error.message}`);
        }
    }

    renderRelayGrid();
    saveToStorage();
}

// Controlar todos os relays
async function controlAllRelays(action) {
    if (relays.length === 0) {
        alert('Nenhum relay cadastrado');
        return;
    }

    logToTerminal(`Enviando comando ${action} para todos os relays...`);

    const promises = relays.map(relay => controlRelay(relay.id, action));
    await Promise.all(promises);
    
    logToTerminal(`Comando ${action} enviado para todos os dispositivos`);
}

// Atualizar status de todos os relays
async function refreshAllRelays() {
    if (relays.length === 0) {
        alert('Nenhum relay cadastrado');
        return;
    }

    logToTerminal('Atualizando status de todos os relays...');

    const promises = relays.map(relay => checkRelayStatus(relay));
    await Promise.all(promises);
    
    logToTerminal('Status de todos os dispositivos atualizado');
}

// Testar conexão com todos os relays
async function testConnection() {
    await refreshAllRelays();
    
    const onlineCount = relays.filter(r => r.online).length;
    const totalCount = relays.length;
    
    logToTerminal(`📡 Teste de conexão concluído: ${onlineCount}/${totalCount} dispositivos online`);
}

// Remover relay
function removeRelay(relayId) {
    const relay = relays.find(r => r.id == relayId);
    if (!relay) return;

    if (confirm(`Tem certeza que deseja remover o relay "${relay.name}"?`)) {
        relays = relays.filter(r => r.id != relayId);
        renderRelayGrid();
        saveToStorage();
        logToTerminal(`Relay removido: ${relay.name}`);
    }
}

// Iniciar atualização periódica
function startPeriodicRefresh() {
    if (refreshInterval) clearInterval(refreshInterval);
    
    refreshInterval = setInterval(() => {
        if (relays.length > 0) {
            refreshAllRelays();
        }
    }, 30000); // Atualizar a cada 30 segundos
}

// Funções de interface
function showLoading(relayId) {
    const card = document.getElementById(`relay-${relayId}`);
    if (card) {
        card.style.opacity = '0.7';
    }
}

function hideLoading(relayId) {
    const card = document.getElementById(`relay-${relayId}`);
    if (card) {
        card.style.opacity = '1';
    }
}

// Log para terminal
function logToTerminal(message) {
    const terminal = document.getElementById('terminal');
    const timestamp = new Date().toLocaleTimeString();
    const logEntry = document.createElement('div');
    logEntry.innerHTML = `<span style="color: #888;">[${timestamp}]</span> ${message}`;
    terminal.appendChild(logEntry);
    terminal.scrollTop = terminal.scrollHeight;
}

// Limpar terminal
function clearTerminal() {
    document.getElementById('terminal').innerHTML = '';
    logToTerminal('Terminal limpo');
}

// Formatação de tempo
function formatTime(date) {
    return new Date(date).toLocaleTimeString();
}

// Salvar no armazenamento local
function saveToStorage() {
    const data = {
        relays: relays,
        syncEnabled: syncEnabled
    };
    // Note: In a real environment, you would use localStorage here
    // localStorage.setItem('relayController', JSON.stringify(data));
}

// Carregar do armazenamento local
function loadRelaysFromStorage() {
    try {
        // Note: In a real environment, you would use localStorage here
        // const saved = localStorage.getItem('relayController');
        // if (saved) {
        //     const data = JSON.parse(saved);
        //     relays = data.relays || [];
        //     syncEnabled = data.syncEnabled !== undefined ? data.syncEnabled : true;
        //     document.getElementById('syncToggle').checked = syncEnabled;
        //     document.getElementById('syncStatus').textContent = syncEnabled ? 'Ativada' : 'Desativada';
        //     renderRelayGrid();
        // }
        
        // Para demonstração, vamos adicionar alguns relays de exemplo
        relays = [
            {
                id: 1,
                name: 'Relay Principal',
                ip: '192.168.4.2',
                status: 'unknown',
                relayState: 'unknown',
                online: false,
                lastUpdate: new Date()
            },
            {
                id: 2,
                name: 'Relay Secundário',
                ip: '192.168.4.3',
                status: 'unknown',
                relayState: 'unknown',
                online: false,
                lastUpdate: new Date()
            }
        ];
        renderRelayGrid();
    } catch (error) {
        console.error('Erro ao carregar dados salvos:', error);
        logToTerminal('⚠️ Erro ao carregar configurações salvas');
    }
}
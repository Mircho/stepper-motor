class ReconnectingWS extends EventTarget {
  _url = ""
  _ws = null
  _retryTime = 0
  _retryTimeout = undefined
  constructor(url, retryTime = 1000) {
    super();
    this._url = url;
    this._retryTime = retryTime;
    this.connect();
  }
  connect() {
    console.log('Connecting to websocket at ', this._url);
    this._ws = new WebSocket(this._url);
    this._ws.addEventListener('open', () => {
      this.dispatchEvent(new Event('open'));
      if (this._retryTimeout) clearTimeout(this._retryTimeout);
    });
    this._ws.addEventListener('message', (message) => {
      this.dispatchEvent(new CustomEvent('message', { detail: JSON.parse(message.data) }));
    })
    this._ws.addEventListener('close', () => {
      this.dispatchEvent(new Event('close'));
      this.scheduleConnect();
    });
    this._ws.addEventListener('error', () => {
      this.dispatchEvent(new Event('error'));
      this.scheduleConnect();
    })
  }
  scheduleConnect() {
    this._ws = null;
    if (this._retryTimeout) clearTimeout(this._retryTimeout);
    this._retryTimeout = setTimeout(() => {
      this.connect();
    }, this._retryTime);
  }
  disconnect() {
    this.ws?.close();
  }
}

const host = `${window.location.host}`;
// const host = `button-test-jig`;

const rpc_endpoint = `http://${host}/rpc/`;

const call_rpc = method => fetch(`${rpc_endpoint}${method}`);

const _ = el => [...document.querySelectorAll(el)];

const connectWebSockets = () => {
  const statusWS = new ReconnectingWS(`ws://${host}`, 1000);
  statusWS.addEventListener( 'message', ({detail}) => {
    const message = detail;
    document.querySelector('#ed\\.start').setAttribute('aria-busy', message.moving ? 'true' : 'false');
  });
}

const updateConfigForms = (status) => {
  document.querySelector('#ed\\.start').setAttribute('aria-busy', status.moving ? 'true' : 'false');
  document.querySelector('#ed\\.freq').value = status.freq;
}

const updateClasses = (status) => {
  Object.keys(status).forEach((statusKey)=>{
    _(`[class*='motor-status-${statusKey}']`).forEach((el) => {
      el.classList.forEach(cls => {
        el.classList.replace(cls, `motor-status-${statusKey}-${status[statusKey]}`);
      })
    })
  })
}

const loadDeviceStatus = async () => {
  const statusRes = await call_rpc('motor.status');
  const status = await statusRes.json();
  return status;
}

const updateFromStatus = async () => {
  const deviceStatus = await loadDeviceStatus();
  updateConfigForms(deviceStatus);
  updateClasses(deviceStatus);
}

const setupHandlers = () => {
  document.querySelector('#ed\\.freq').onchange = async (ev) => {
    const _method = `motor.setfrequency?freq=${ev.target.value}`; 
    await call_rpc(_method);
  }

  document.querySelector('#ed\\.step').onclick = async (ev) => {
    const steps = document.querySelector('#ed\\.steps').value;
    const _method = `motor.step?steps=${steps}`; 
    await call_rpc(_method);
  }

  [...document.querySelectorAll("button[formaction*='motor']")].forEach( el=> {
    el.onclick = async (ev) => {
      ev.preventDefault();
      const _u = new URL(el.formAction);
      await call_rpc(_u.pathname.substring(1));
      updateFromStatus();
    }
  })
}

window.addEventListener('load', async (event) => {
  connectWebSockets();
  updateFromStatus();
  setupHandlers();
})
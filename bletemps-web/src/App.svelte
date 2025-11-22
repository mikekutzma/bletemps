<script lang="ts">
    let device: BluetoothDevice | undefined;
    let server: BluetoothRemoteGATTServer | undefined;
    let service: BluetoothRemoteGATTService | undefined;
    let notifyChar: BluetoothRemoteGATTCharacteristic | undefined;

    let scanning = false;
    let connected = false;
    let bmp_error: string | null = null;
    let temperature: number | null = null;
    let pressure: number | null = null;
    let humidity: number | null = null;

    const SERVICE_UUID = "4b910000-c9c5-cc8f-9e45-b51f01c2af4f";
    const READING_NOTIFY_CHAR_UUID = "4b910004-c9c5-cc8f-9e45-b51f01c2af4f";

    const celsiusToFahrenheit = (degrees: number | null) => {
        if (degrees == null) {
            return degrees;
        }
        return 1.8 * degrees + 32;
    };

    const startScan = async () => {
        scanning = true;
        try {
            device = await navigator.bluetooth.requestDevice({
                filters: [{ namePrefix: "OpenClicker" }],
                // acceptAllDevices: true,
                optionalServices: [SERVICE_UUID],
            });
            console.log(`Selected ${device.name}`);
        } catch (e) {
            console.log(`Scan error: ${e}`);
        }
        scanning = false;
        await connect();
    };

    const connect = async () => {
        if (!device) return;
        try {
            server = await device.gatt!.connect();
            service = await server.getPrimaryService(SERVICE_UUID);
            connected = true;
            console.log("Connected");
            await subscribeNotify();
            readAll();
        } catch (e) {
            console.log(`Connect error: ${e}`);
        }
    };

    const disconnect = () => {
        if (server) server.disconnect();
        connected = false;
        console.log("Disconnected");
    };

    const subscribeNotify = async () => {
        if (!service) return;
        try {
            notifyChar = await service.getCharacteristic(
                READING_NOTIFY_CHAR_UUID,
            );
            await notifyChar.startNotifications();
            notifyChar.addEventListener("characteristicvaluechanged", onNotify);
            console.log("Notifications enabled");
        } catch (e) {
            console.log(`Notify subscribe error: ${e}`);
        }
    };

    const updateValues = (data_str: string) => {
        let data;
        try {
            data = JSON.parse(data_str);
        } catch (e) {
            console.error("Invalid JSON in notification:", e);
            return;
        }
        bmp_error = data.error;
        temperature = data.temperature;
        pressure = data.pressure / 1000.0;
        humidity = data.humidity;
    };
    const onNotify = (event: Event) => {
        const val = (event.target as BluetoothRemoteGATTCharacteristic).value!;
        const decoded = new TextDecoder().decode(val);
        console.log(`Notification: ${decoded}`);
        updateValues(decoded);
    };

    const readAll = async () => {
        if (!service) return;
        const char = await service.getCharacteristic(READING_NOTIFY_CHAR_UUID);
        const decoded = new TextDecoder().decode(await char.readValue());
        console.log(`Read Values: ${decoded}`);
        updateValues(decoded);
    };
</script>

<main>
    <button
        disabled={scanning}
        on:click={startScan}
        class="ble-btn"
        aria-label="Connect Bluetooth"
    >
        Find Devices
    </button>

    <section class="cards">
        <div class="card">
            <span class="label">Temperature</span>
            <span class="value"
                >{celsiusToFahrenheit(temperature)?.toFixed(1)}°F</span
            >
        </div>
        <div class="card">
            <span class="label">Pressure</span>
            <span class="value">{pressure?.toFixed(1)} kPa</span>
        </div>
        <div class="card">
            <span class="label">Humidity</span>
            <span class="value">{humidity?.toFixed(1)}%</span>
        </div>
    </section>
</main>

<style>
    :global(body) {
        margin: 0;
        font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto,
            Helvetica, Arial, sans-serif;
        background: #0d1117;
        color: #ffffff;
    }

    main {
        display: flex;
        flex-direction: column;
        align-items: center;
        padding: 2rem;
        min-height: 100vh;
        box-sizing: border-box;
    }

    .ble-btn {
        position: absolute;
        top: 1rem;
        left: 1rem;
        background: #161b22;
        border: 1px solid #30363d;
        border-radius: 0.5rem;
        padding: 0.5rem;
        cursor: pointer;
        transition: background 0.2s;
    }
    .ble-btn:hover {
        background: #21262d;
    }
    .ble-btn svg {
        fill: #58a6ff;
    }

    .cards {
        width: 100%;
        max-width: 28rem;
        display: flex;
        flex-direction: column;
        gap: 1.5rem;
        margin-top: 4rem;
    }

    .card {
        background: #161b22;
        border: 1px solid #30363d;
        border-radius: 1rem;
        padding: 2rem;
        display: flex;
        flex-direction: column;
        align-items: center;
        gap: 0.5rem;
    }

    .label {
        font-size: 1rem;
        color: #8b949e;
    }

    .value {
        font-size: 3rem;
        font-weight: 600;
    }

    @media (min-width: 640px) {
        .cards {
            flex-direction: row;
            max-width: 60rem;
        }
        .card {
            flex: 1;
        }
    }
</style>

export interface PulseData {
 opticalReady: boolean;
 bpm: number | null;
 bpmHeld: boolean;
 spo2: number | null;
 oxygenTrialSupported: boolean;
 oxygenTrial: number | null;
 uptimeSeconds: number;
}

export interface ActivityData {
 steps: number | null;
 epochSeconds: number | null;
 utcOffsetMinutes: number | null;
 moving: boolean;
 redWeak: boolean;
 imuTemperatureC: number | null;
}

export interface FitnessWatchCallbacks {
 onPulse?: (data: PulseData) => void;
 onActivity?: (data: ActivityData) => void;
 onRaw?: (data: {red: number; ir: number} | null) => void;
 onMotion?: (data: {accel: number[]; gyro: number[]} | null) => void;
 onDisconnect?: () => void;
 onError?: (error: Error) => void;
}

export function connectFitnessWatch(callbacks?: FitnessWatchCallbacks): Promise<{
 disconnect(): void;
 syncAqi(): Promise<void>;
 getScreenOrder(): Promise<number[] | null>;
 setScreenOrder(order: readonly number[]): Promise<void>;
}>;
export const WATCH_SCREEN_NAMES: readonly string[];
export function decodePulse(packet: DataView): PulseData;
export function decodeActivity(packet: DataView): ActivityData;
export function encodeClock(date: Date): DataView;

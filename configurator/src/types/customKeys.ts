/**
 * Custom Key types — previously defined in HIDService.ts.
 */

export interface CustomKeyPR {
    pressAction: number;
    releaseAction: number;
    pressDuration: number;
    releaseDuration: number;
    waitForFinish: boolean;
    pressSustain: boolean;
}

export const CustomKeyFlavor = {
    Timer: 0,
    HoldOnOtherKeyPress: 1,
    PermissiveHold: 2,
    TapPreferred: 3,
} as const;

export type CustomKeyFlavor = typeof CustomKeyFlavor[keyof typeof CustomKeyFlavor];

export interface CustomKeyMA {
    tapAction: number;
    doubleTapAction: number;
    holdAction: number;
    doubleTapThreshold: number;
    holdThreshold: number;
    tapDuration: number;
    doubleTapDuration: number;
    holdDuration: number;
    holdSustain: boolean;
    flavor?: CustomKeyFlavor;
    quickTapMs?: number;
    requirePriorIdleMs?: number;
}

export interface CustomKey {
    id: number;
    name: string;
    mode: number; // 0 = PressRelease, 1 = MultiAction
    pr?: CustomKeyPR;
    ma?: CustomKeyMA;
}

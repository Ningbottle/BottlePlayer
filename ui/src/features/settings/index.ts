export { default as SettingsView } from './SettingsView.vue';
export { default as EqualizerView } from './EqualizerView.vue';
export {
  fetchDiagnosticsMemory,
  fetchDeviceSettings,
  saveDeviceSettings,
  resetDeviceSettings,
  type MemoryData,
  type DiagnosticsMemoryResponse,
  type DeviceInfo,
  type DeviceSettingsResponse,
  type SaveDeviceSettingsResponse,
} from './settingsGateway';

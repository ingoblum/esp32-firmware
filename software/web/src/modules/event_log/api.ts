export interface syslog_config {
    enabled: boolean;
    host: string;
    port: number;
}

export type message = string;

export interface boot_id {
    boot_id: number;
}

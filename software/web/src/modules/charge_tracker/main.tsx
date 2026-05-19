/* esp32-firmware
 * Copyright (C) 2020-2021 Erik Fleckstein <erik@tinkerforge.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

//#include "generated/module_available.inc"

import * as util from "../../ts/util";
import * as API from "../../ts/api";
import * as options from "../../options";
import { h, Fragment, Component, RefObject, createRef } from "preact";
import { effect } from "@preact/signals-core";
import { __, get_active_language_enum } from "../../ts/translation";
import { FormRow } from "../../ts/components/form_row";
import { FormSeparator } from "../../ts/components/form_separator";
import { InputText } from "../../ts/components/input_text";
import { InputDate } from "../../ts/components/input_date";
import { Button, Collapse, ListGroup, ListGroupItem, Spinner, Dropdown, Modal } from "react-bootstrap";
import { InputSelect } from "../../ts/components/input_select";
import { ConfigComponent } from "../../ts/components/config_component";
import { ConfigForm } from "../../ts/components/config_form";
import { InputFloat } from "../../ts/components/input_float";
import { SubPage } from "../../ts/components/sub_page";
import { Table, TableRow } from "../../ts/components/table";
import { useMemo } from "preact/hooks";
import { UplotData, UplotWrapperB, UplotPath } from "../../ts/components/uplot_wrapper_2nd";
import { NavbarItem } from "../../ts/components/navbar_item";
import { StatusSection } from "../../ts/components/status_section";
import { BatteryCharging, Calendar, Clock, Download, User, List, Send, Mail } from "react-feather";
import { CSVFlavor } from "./generated/csv_flavor.enum";
import { Language } from "../../ts/language";
import { GenerationState } from "./generated/generation_state.enum";

export function ChargeTrackerNavbar() {
    return <NavbarItem name="charge_tracker" module="charge_tracker" title={__("charge_tracker.navbar.charge_tracker")} symbol={<List />} />;
}

const MAX_TRACKED_CHARGES = 7680;
const CHARGE_DYNAMIC_COST_UNAVAILABLE = 0xFFFFFFFF;

type Charge = API.getType["charge_tracker/last_charges"][0];
type ChargeTrackerConfig = API.getType["charge_tracker/config"];

interface ChargeHistorySample {
    t: number;
    w: number;
    ct: number;
}

interface ChargeHistoryResponse {
    tag_type?: number;
    tag_id?: string;
    samples?: ChargeHistorySample[];
}

interface S {
    user_filter: string;
    start_date: Date;
    end_date: Date;
    file_type: string;
    language: string;
    pdf_letterhead: string;
    csv_flavor: "excel" | "rfc4180";
    show_spinner: boolean;
    last_charges: Readonly<Charge[]>;
    history_modal_charge: Charge | null;
    history_modal_tag_type: number;
    history_modal_tag_id: string;
    history_modal_samples: ChargeHistorySample[];
//#if MODULE_REMOTE_ACCESS_AVAILABLE
    new_remote_upload_config: {
        user_filter: number;
        file_type: number;
        language: Language;
        letterhead: string;
        user_id: number;
        csv_delimiter: CSVFlavor;
        last_upload_timestamp_min: number;
    };
    next_upload_timestamp_min: number;
//#endif
}

type ChargeTrackerState = S & API.getType['charge_tracker/state'];

let wallet_icon = <svg width="24" height="24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><rect x="1" y="6.0999" width="22" height="16" rx="2" ry="2"/><path d="m2.9474 6.0908 15.599-4.8048s0.59352-0.22385 0.57647 0.62527c-0.02215 1.1038-0.01535 3.6833-0.01535 3.6833"/></svg>

function TrackedCharge(props: {charge: Charge, users: API.getType['users/config']['users'], electricity_price: number, onClick?: () => void}) {
    const display_name = useMemo(
        () => {
            let result = __("charge_tracker.script.unknown_user");

            let filtered = props.users.filter(x => x.id == props.charge.user_id);

            if (filtered.length == 1) {
                if (props.charge.user_id != 0 || filtered[0].display_name != "Anonymous")
                    result = filtered[0].display_name;
            }
            else if (props.charge.user_id != 0) {
                result = __("charge_tracker.script.deleted_user");
            }
            return result;
        },
        [props.users, props.charge.user_id, get_active_language_enum()]
    );

    // Prefer the backend's dynamic Day Ahead cost when present. Older records
    // and records without a valid dynamic price keep using the configured fixed
    // electricity price so the UI stays backward compatible with existing logs.
    let have_dynamic_cost = props.charge.dynamic_cost != null // Safety check against null, however a null value shouldn't occur.
        && props.charge.dynamic_cost != CHARGE_DYNAMIC_COST_UNAVAILABLE; // instead CHARGE_DYNAMIC_COST_UNAVAILABLE is used to indicate a missing dynamic cost values
        
    let have_charge_cost = have_dynamic_cost || (props.electricity_price > 0 && props.charge.energy_charged != null);
    let charge_cost = have_dynamic_cost
        ? props.charge.dynamic_cost / 100
        : props.electricity_price / 100 * props.charge.energy_charged / 100;

    return <ListGroupItem action={props.onClick != null} onClick={props.onClick}>
        <div class="row justify-content-end">
            <div class="col-auto pe-2 mb-2">
                <span class="pe-2"><User/></span>
                <span style="vertical-align: middle;">{display_name}</span>
            </div>
            <div class="col px-0" />
            <div class="col-auto ps-2 mb-2">
                <span style="vertical-align: middle;">{props.charge.energy_charged === null ? "N/A" : util.toLocaleFixed(props.charge.energy_charged, 3)} kWh</span>
                <span class="ps-2"><BatteryCharging/></span>
            </div>
        </div>
        <div class={"row justify-content-end" + (have_charge_cost ? "" : " mb-n2")}>
            <div class="col-auto pe-2 mb-2">
                <span class="pe-2"><Calendar/></span>
                <span style="vertical-align: middle;">{util.timestamp_min_to_date(props.charge.timestamp_minutes, __("charge_tracker.script.unknown_charge_start"))}</span>
            </div>
            <div class="col px-0" />
            <div class="col-auto ps-2 mb-2">
                <span style="vertical-align: middle;">{util.format_timespan(props.charge.charge_duration)}</span>
                <span class="ps-2"><Clock/></span>
            </div>
        </div>
        {have_charge_cost ?
            <div class="row justify-content-end mb-n2">
                <div class="col px-0" />
                <div class="col-auto ps-2 mb-2">
                    <span style="vertical-align: middle;">{util.toLocaleFixed(charge_cost, 2)} EUR</span>
                    <span class="ps-2">{wallet_icon}</span>
                </div>
            </div> : undefined}
    </ListGroupItem>
}

class ChargeHistoryGraph extends Component<{samples: ChargeHistorySample[], charge_start_timestamp_min: number}, {}> {
    power_price_ref = createRef<UplotWrapperB>();
    cost_ref = createRef<UplotWrapperB>();
    sync = {key: "charge_tracker_history"} as any;
    static readonly COST_BIN_SECONDS = 15 * 60;

    override componentDidMount() {
        this.update_uplot();
    }

    override componentDidUpdate() {
        this.update_uplot();
    }

    /**
     * Prepares data for the Power and Price chart (upper chart).
     * 
     * This method converts the raw history samples into a format suitable for uPlot.
     * It uses a Step-path to show the power and price changes over time.
     * 
     * @param historySamples Raw samples from the hardware
     * @param chargeStartSeconds Absolute start timestamp of the charge in seconds
     */
    build_power_price_data(historySamples: ChargeHistorySample[], chargeStartSeconds: number): UplotData {
        const lastSample = historySamples[historySamples.length - 1];
        const chargeEndSeconds = lastSample == null ? chargeStartSeconds : chargeStartSeconds + Math.max(1, lastSample.t) * 60;
        
        const uplotData: UplotData = {
            keys: [null, "power", "price"],
            names: [null, "Leistung", "Börsenpreis inkl. MwSt."],
            values: [[], [], []],
            paths: [null, UplotPath.Step, UplotPath.Step],
            y_axes: [null, "y", "y2"],
            lines_vertical: [
                {index: 0, text: "Start", color: [64, 64, 64, 0.10]},
                {index: historySamples.length, text: "Ende", color: [64, 64, 64, 0.10]},
            ],
        };

        // Add the initial data point at the start of the charge
        uplotData.values[0].push(chargeStartSeconds);
        uplotData.values[1].push(historySamples[0].w / 1000.0); // Convert Watts to kW
        uplotData.values[2].push(historySamples[0].ct == 0x7FFFFFFF ? null : historySamples[0].ct / 1000.0); // Convert milli-ct to ct/kWh

        // Add all intermediate samples
        for (const sample of historySamples) {
            uplotData.values[0].push(chargeStartSeconds + sample.t * 60);
            uplotData.values[1].push(sample.w / 1000.0);
            uplotData.values[2].push(sample.ct == 0x7FFFFFFF ? null : sample.ct / 1000.0);
        }

        // Add a final anchor point to ensure the last step is drawn correctly until the end of the charge
        if (lastSample != null) {
            uplotData.values[0].push(chargeEndSeconds);
            uplotData.values[1].push(lastSample.w / 1000.0);
            uplotData.values[2].push(lastSample.ct == 0x7FFFFFFF ? null : lastSample.ct / 1000.0);
        }

        return uplotData;
    }

    /**
     * Prepares data for the Cost Accumulation chart (lower chart).
     * 
     * This method aggregates energy costs into fixed 15-minute time bins.
     * Energy and costs are linearly distributed if a sample interval spans multiple bins.
     * 
     * @param historySamples Raw samples from the hardware
     * @param chargeStartSeconds Absolute start timestamp of the charge in seconds
     */
    build_cost_data(historySamples: ChargeHistorySample[], chargeStartSeconds: number): UplotData {
        // Align the first bin to a 15-minute calendar boundary (e.g., 12:00, 12:15, 12:30)
        const alignedStartTimestampSeconds = Math.floor(chargeStartSeconds / ChargeHistoryGraph.COST_BIN_SECONDS) * ChargeHistoryGraph.COST_BIN_SECONDS;
        
        const lastSample = historySamples[historySamples.length - 1];
        const chargeEndSeconds = chargeStartSeconds + Math.max(1, lastSample.t) * 60;
        
        // Calculate how many 15-minute bins we need to cover the entire charge duration
        const totalBinCount = Math.max(1, Math.ceil((chargeEndSeconds - alignedStartTimestampSeconds) / ChargeHistoryGraph.COST_BIN_SECONDS));
        const accumulatedCostsPerBin = new Array(totalBinCount).fill(0);

        let previousSampleOffsetMinutes = 0;
        for (const sample of historySamples) {
            const segmentDurationMinutes = Math.max(0, sample.t - previousSampleOffsetMinutes);
            const segmentStartSeconds = chargeStartSeconds + previousSampleOffsetMinutes * 60;
            const segmentEndSeconds = chargeStartSeconds + sample.t * 60;
            previousSampleOffsetMinutes = sample.t;

            // Skip zero-length intervals or invalid price data
            if (segmentDurationMinutes == 0 || sample.ct == 0x7FFFFFFF) {
                continue;
            }

            const currentPriceCtPerKwh = sample.ct / 1000.0;
            let currentTimestampSeconds = segmentStartSeconds;

            // Distribute energy from this sample interval across the affected time bins
            while (currentTimestampSeconds < segmentEndSeconds) {
                const currentBinIndex = Math.floor((currentTimestampSeconds - alignedStartTimestampSeconds) / ChargeHistoryGraph.COST_BIN_SECONDS);
                const currentBinEndSeconds = alignedStartTimestampSeconds + (currentBinIndex + 1) * ChargeHistoryGraph.COST_BIN_SECONDS;
                
                // End of the current part: either the end of the current bin or the end of the sample interval
                const partEndSeconds = Math.min(segmentEndSeconds, currentBinEndSeconds);
                const partDurationMinutes = Math.max(0, (partEndSeconds - currentTimestampSeconds) / 60.0);
                
                // Calculate energy consumed in this specific part of the interval (W -> kWh)
                const partEnergyKwh = (sample.w / 1000.0) * (partDurationMinutes / 60.0);

                if (currentBinIndex >= 0 && currentBinIndex < accumulatedCostsPerBin.length) {
                    accumulatedCostsPerBin[currentBinIndex] += partEnergyKwh * currentPriceCtPerKwh;
                }

                currentTimestampSeconds = partEndSeconds;
            }
        }

        const uplotData: UplotData = {
            keys: [null, "cost"],
            names: [null, "Gesamtpreis je 15 min"],
            values: [[], []],
            filled: [null, true],
            paths: [null, UplotPath.Bar],
            lines_vertical: [
                {index: 0, text: "Start", color: [64, 64, 64, 0.10]},
                {index: totalBinCount - 1, text: "Ende", color: [64, 64, 64, 0.10]},
            ],
        };

        // Fill the uPlot data arrays with the aggregated costs
        for (let i = 0; i < accumulatedCostsPerBin.length; ++i) {
            uplotData.values[0].push(alignedStartTimestampSeconds + i * ChargeHistoryGraph.COST_BIN_SECONDS);
            uplotData.values[1].push(accumulatedCostsPerBin[i]);
        }

        // Force identical x-range with the upper chart so timestamps line up visually.
        uplotData.values[0].push(chargeEndSeconds);
        uplotData.values[1].push(null);

        return uplotData;
    }

    update_uplot() {
        if (this.power_price_ref.current == null || this.cost_ref.current == null) {
            return;
        }

        const samples = this.props.samples ?? [];
        if (samples.length == 0) {
            this.power_price_ref.current.set_data({keys: [null], names: [null], values: [null]});
            this.cost_ref.current.set_data({keys: [null], names: [null], values: [null]});
            return;
        }

        const start_seconds = this.props.charge_start_timestamp_min * 60;
        this.power_price_ref.current.set_data(this.build_power_price_data(samples, start_seconds));
        this.cost_ref.current.set_data(this.build_cost_data(samples, start_seconds));
    }

    override render() {
        const samples = this.props.samples ?? [];
        if (samples.length == 0) {
            return <div class="text-muted">Keine Verlaufsdaten vorhanden.</div>;
        }

        return <div style="position: relative;">
            <UplotWrapperB
                ref={this.power_price_ref}
                class="charge-history-power-price-chart"
                sub_page="charge_tracker"
                color_cache_group="charge_tracker.history"
                show
                on_mount={() => this.update_uplot()}
                sync={this.sync}
                legend_time_label="Zeitbereich"
                legend_time_with_minutes
                aspect_ratio={2}
                x_format={{hour: "2-digit", minute: "2-digit"}}
                x_padding_factor={0}
                x_include_date={false}
                y_min={0}
                y_unit="kW"
                y_label="Leistung"
                y_digits={2}
                y2_enable
                y2_unit="ct/kWh"
                y2_label="Börsenpreis inkl. MwSt."
                y2_digits={3}
                y_sync_ref={this.cost_ref as any}
                padding={[25, 15, null, null]}
                height_min={360}
            />
            <div class="mt-2">
                <UplotWrapperB
                    ref={this.cost_ref}
                    class="charge-history-cost-chart"
                    sub_page="charge_tracker"
                    color_cache_group="charge_tracker.history.cost"
                    show
                    on_mount={() => this.update_uplot()}
                    sync={this.sync}
                    legend_time_label="Zeitbereich"
                    legend_time_with_minutes
                    aspect_ratio={4}
                    x_format={{hour: "2-digit", minute: "2-digit"}}
                    x_padding_factor={0}
                    x_include_date={false}
                    y_min={0}
                    y_unit="ct"
                    y_label="Gesamtpreis"
                    y_digits={3}
                    y2_enable
                    y2_unit="ct/kWh"
                    y2_label="BÃ¶rsenpreis inkl. MwSt."
                    y2_digits={3}
                    y_sync_ref={this.power_price_ref as any}
                    padding={[15, 15, null, null]}
                    height_min={180}
                />
            </div>
        </div>;
    }
}


function date_to_minutes(d: Date, round_mode: 'start_of_day' | 'end_of_day') {
    let date = d ?? new Date(0);
    // dates are "invalid date" (i.e. getTime returns NaN) if the user clicks an input's clear button.
    if (isNaN(date.getTime()))
        date = new Date(0);

    if (date.getTime() != 0) {
        switch (round_mode) {
            case 'start_of_day': date.setHours( 0,  0, 0, 0); break;
            case 'end_of_day':   date.setHours(23, 59, 0, 0); break;
        }
    }

    return date.getTime() / 1000 / 60;
}

export class ChargeTracker extends ConfigComponent<'charge_tracker/config', {status_ref?: RefObject<ChargeTrackerStatus>}, ChargeTrackerState> {
    constructor() {
        super('charge_tracker/config',
              () => __("charge_tracker.script.save_failed"),
              undefined, {
                  user_filter: "-2",
                  file_type: "0",
                  language: null,
                  csv_flavor: 'excel',
                  start_date: new Date(NaN),
                  end_date: new Date(NaN),
                  history_modal_charge: null,
                  history_modal_tag_type: 0,
                  history_modal_tag_id: "",
                  history_modal_samples: [],
//#if MODULE_REMOTE_ACCESS_AVAILABLE
                  remote_upload_configs: [],
                  new_remote_upload_config: {
                    user_filter: -2,
                    file_type: 0,
                    language: get_active_language_enum(),
                    letterhead: "",
                    user_id: 0,
                    csv_delimiter: CSVFlavor.Excel,
                    last_upload_timestamp_min: 0,
                  },
                  next_upload_timestamp_min: 0,
//#endif
              });

        effect(() => {
            // set initial value on language change
            let language_enum = get_active_language_enum();

            if (util.render_allowed() && this.state.language == null) {
                this.setState({language: language_enum.toString()});
            }
        });

        util.addApiEventListener('charge_tracker/state', () => {
            this.setState({...API.get('charge_tracker/state')});
        });

        util.addApiEventListener('charge_tracker/last_charges', () => {
            this.setState({last_charges: API.get('charge_tracker/last_charges')});
        });

//#if MODULE_REMOTE_ACCESS_AVAILABLE
        util.addApiEventListener('charge_tracker/config', () => {
            let config = API.get('charge_tracker/config');
            let last_upload_timestamp_min = 0;
            for (const cfg of config.remote_upload_configs) {
                if (last_upload_timestamp_min < cfg.last_upload_timestamp_min) {
                    last_upload_timestamp_min = cfg.last_upload_timestamp_min;
                }
            }

            let next_upload_timestamp_min = 0;
            if (last_upload_timestamp_min != 0) {
                const date = new Date(last_upload_timestamp_min * 60 * 1000);
                date.setDate(1);
                date.setMonth(date.getMonth() + 1);
                date.setHours(0, 0, 0, 0);
                next_upload_timestamp_min = Math.floor(date.getTime() / 1000 / 60);
            }

            this.setState({
                electricity_price: config.electricity_price,
                remote_upload_configs: config.remote_upload_configs,
                next_upload_timestamp_min,
            });
        });

        util.addApiEventListener('charge_tracker/upload_result', () => {
            let upload_result = API.get('charge_tracker/upload_result');

            const result = JSON.parse(JSON.stringify(upload_result)); // deep copy since it would be overridden later otherwise
            const alertName = "charge-log-upload-" + result.cookie;
            util.add_alert(
                alertName,
                result.error === "" ? "success" : "danger",
                () => result.error === "" ? __("charge_tracker.script.upload_charge_log_success") : __("charge_tracker.script.upload_charge_log_failed"),
                () => result.error
            );
            if (result.error === "") {
                setTimeout(() => util.remove_alert(alertName), 5000);
            }
        });
//#endif

        util.addApiEventListener('charge_tracker/pdf_letterhead_config', () => {
            let pdf_letterhead_config = API.get('charge_tracker/pdf_letterhead_config');
            this.setState({pdf_letterhead: pdf_letterhead_config.letterhead});
        });
    }

    get_last_charges(charges: Readonly<Charge[]>, price: number) {
        let users_config = API.get('users/config');

        return charges.map(c => <TrackedCharge charge={c} users={users_config.users} electricity_price={price} onClick={() => this.showChargeHistory(c)}/>)
                      .reverse();
    }

    async showChargeHistory(charge: Charge) {
        this.setState({history_modal_charge: charge, history_modal_tag_type: 0, history_modal_tag_id: "", history_modal_samples: []});

        try {
            // Use fields that are already present in last_charges instead of
            // adding backend file/record indices to the WebSocket state. The
            // latter overflowed the WARP 1 initial WebSocket payload buffer
            // when repeated across the 30 visible charge records.
            const history_queries = [
                `/charge_tracker/charge_history?timestamp=${charge.timestamp_minutes}&duration=${charge.charge_duration}`,
            ];

            // Compatibility fallback for older firmware that still keys dynamic
            // history lookups by file/record indices instead of timestamp/duration.
            const legacy_charge = charge as Charge & {file_index?: number; record_index?: number};
            if (legacy_charge.file_index != null && legacy_charge.record_index != null) {
                history_queries.push(`/charge_tracker/charge_history?file_index=${legacy_charge.file_index}&record_index=${legacy_charge.record_index}`);
                history_queries.push(`/charge_tracker/charge_history?file=${legacy_charge.file_index}&record=${legacy_charge.record_index}`);
            }

            let response: Response | null = null;
            for (const query of history_queries) {
                response = await fetch(query);
                if (response.ok) {
                    break;
                }
            }

            if (response == null || !response.ok) {
                throw new Error(await response?.text() ?? "charge history request failed");
            }

            const history = await response.json() as ChargeHistoryResponse;
            this.setState({
                history_modal_tag_type: history.tag_type ?? 0,
                history_modal_tag_id: history.tag_id ?? "",
                history_modal_samples: history.samples ?? []
            });
        } catch (e) {
            util.add_alert("charge-history-download", "danger", () => "Ladeverlauf konnte nicht geladen werden", e);
        }
    }

//#if MODULE_REMOTE_ACCESS_AVAILABLE
    getRemoteUploadConfigTableRows(user_filter_items: [string, string][]): TableRow[] {
        const remote_upload_configs = this.state.remote_upload_configs;
        const users_config = API.get('users/config');

        return remote_upload_configs.map((send_config, index) => {
            const user_filter_str = send_config.user_filter === -2 ? __("charge_tracker.script.all_users") :
                                  send_config.user_filter === -1 ? __("charge_tracker.script.deleted_users") :
                                  this.get_user_display_name(send_config.user_filter, users_config.users);

            const file_type_str = send_config.file_type === 0 ? "PDF" : "CSV";
            const target_user_str = this.get_remote_access_user_email(send_config.user_id, API.get("remote_access/config").users);

            return {
                key: `remote_upload_config_${index}`,
                columnValues: [
                    user_filter_str,
                    file_type_str,
                    target_user_str,
                ],
                onEditShow: async () => {
                    this.setState({
                        new_remote_upload_config: {
                            user_filter: send_config.user_filter,
                            file_type: send_config.file_type,
                            language: send_config.language,
                            letterhead: send_config.letterhead,
                            user_id: send_config.user_id,
                            csv_delimiter: send_config.csv_delimiter,
                            last_upload_timestamp_min: send_config.last_upload_timestamp_min,
                        }
                    });
                },
                editTitle: __("charge_tracker.content.charge_log_email_send_edit_modal_title"),
                onEditGetChildren: () => this.onAddRemoteUploadConfigGetChildren(user_filter_items),
                editAdditionalFooterButtons: (
                    <Button
                        variant="info"
                        disabled={this.state.new_remote_upload_config.user_id === 0 || this.state.generator_state != GenerationState.Ready}
                        onClick={async (e) => {
                            e.preventDefault();
                            await this.testSendChargeLog();
                        }}
                    >
                        <span class="me-2"><Send size={16}/></span>
                        {this.state.generator_state === GenerationState.ManualRemoteSend ? __("charge_tracker.content.sending") : __("charge_tracker.content.test_send")}
                        {this.state.generator_state === GenerationState.ManualRemoteSend && <Spinner animation="border" size="sm" as="span" class="ms-2"/>}
                    </Button>
                ),
                onRemoveClick: async () => {
                    const newState = this.state.remote_upload_configs.filter((_, i) => i !== index);
                    this.setState({remote_upload_configs: newState});
                    this.setDirty(true);
                    return true;
                },
                onEditSubmit: async () => {
                    const remote_upload_configs = this.state.remote_upload_configs;
                    remote_upload_configs[index] = this.state.new_remote_upload_config;
                    this.setDirty(true);
                },
            };
        });
    }

    get_user_display_name(user_id: number, users: API.getType['users/config']['users']): string {
        let result = __("charge_tracker.script.unknown_user");
        let filtered = users.filter(x => x.id == user_id);

        if (filtered.length == 1) {
            if (user_id != 0 || filtered[0].display_name != "Anonymous")
                result = filtered[0].display_name;
        }
        else if (user_id != 0) {
            result = __("charge_tracker.script.deleted_user");
        }
        return result;
    }

    get_remote_access_user_email(user_id: number, users: API.getType['remote_access/config']['users']): string {
        const user = users.find(u => u.id === user_id);
        return user.email
    }

    async testSendChargeLog() {
        try {
            const config = this.state.new_remote_upload_config;
            const remote_access_config = API.get("remote_access/config");
            const user = remote_access_config.users.find(u => u.id === config.user_id);

            // Get last month's time range
            const now = new Date();
            const startOfLastMonth = new Date(now.getFullYear(), now.getMonth() - 1, 1);
            startOfLastMonth.setHours(0, 0, 0, 0);
            const endOfLastMonth = new Date(now.getFullYear(), now.getMonth(), 0);
            endOfLastMonth.setHours(23, 59, 0, 0);

            let start_minutes = Math.floor(startOfLastMonth.getTime() / 1000 / 60);
            let end_minutes = Math.floor(endOfLastMonth.getTime() / 1000 / 60);

            await API.call("charge_tracker/send_charge_log", {
                api_not_final_acked: true,
                language: config.language,
                start_timestamp_min: start_minutes,
                end_timestamp_min: end_minutes,
                user_filter: config.user_filter,
                file_type: config.file_type,
                letterhead: config.letterhead,
                csv_delimiter: config.csv_delimiter,
                cookie: Math.floor(Math.random() * 0xFFFFFFFF),
                remote_access_user_uuid: user.uuid
            }, () => __("charge_tracker.script.upload_charge_log_failed"));

            this.setState({generator_state: GenerationState.ManualRemoteSend});
            util.add_alert("test-charge-log-send", "info", () => __("charge_tracker.script.test_charge_log_upload_started"), () => "");
            setTimeout(() => util.remove_alert("test-charge-log-send"), 3000);
        } catch (err) {}
    }

    async onAddRemoteUploadConfigShow() {
        this.setState({
            new_remote_upload_config: {
                user_filter: -2,
                file_type: 0,
                language: get_active_language_enum(),
                letterhead: "",
                user_id: 0,
                csv_delimiter: CSVFlavor.Excel,
                last_upload_timestamp_min: Math.floor(Date.now() / 1000 / 60),
            }
        });
    }

    async onAddRemoteUploadConfigSubmit() {
        const remote_upload_configs = this.state.remote_upload_configs;
        remote_upload_configs.push(this.state.new_remote_upload_config);
        this.setState({remote_upload_configs});
        this.setDirty(true);
    }

    onAddRemoteUploadConfigGetChildren(user_filter_items: [string, string][]) {
        const remote_access_config = API.get("remote_access/config").users;
        const remote_access_user_items: [string, string][] = remote_access_config.map(u => [u.id.toString(), u.email]);

        return <>
            <FormRow label={__("charge_tracker.content.user_filter_label")}>
                <InputSelect
                    value={this.state.new_remote_upload_config.user_filter.toString()}
                    onValue={v => this.setState({
                        new_remote_upload_config: {
                            ...this.state.new_remote_upload_config,
                            user_filter: parseInt(v)
                        }
                    })}
                    items={user_filter_items}
                />
            </FormRow>
            <FormRow label={__("charge_tracker.content.file_type_label")}>
                <InputSelect
                    value={this.state.new_remote_upload_config.file_type.toString()}
                    onValue={v => this.setState({
                        new_remote_upload_config: {
                            ...this.state.new_remote_upload_config,
                            file_type: parseInt(v)
                        }
                    })}
                    items={[
                        ["0", "PDF"],
                        ["1", "CSV"]
                    ]}
                />
            </FormRow>
            <FormRow label={__("charge_tracker.content.language_label")}>
                <InputSelect
                    value={this.state.new_remote_upload_config.language.toString()}
                    onValue={v => this.setState({
                        new_remote_upload_config: {
                            ...this.state.new_remote_upload_config,
                            language: parseInt(v) as Language
                        }
                    })}
                    items={[
                        [Language.German.toString(), __("charge_tracker.content.language_german")],
                        [Language.English.toString(), __("charge_tracker.content.language_english")]
                    ]}
                />
            </FormRow>
            <Collapse in={this.state.new_remote_upload_config.file_type === 0}>
                <div>
                    <FormRow label={__("charge_tracker.content.letterhead_label")}>
                        <textarea
                            class="form-control"
                            value={this.state.new_remote_upload_config.letterhead}
                            onInput={(e) => {
                                const value = (e.target as HTMLTextAreaElement).value;
                                this.setState({
                                    new_remote_upload_config: {
                                        ...this.state.new_remote_upload_config,
                                        letterhead: value
                                    }
                                });
                            }}
                            rows={3}
                        />
                    </FormRow>
                </div>
            </Collapse>
            <Collapse in={this.state.new_remote_upload_config.file_type === 1}>
                <div>
                    <FormRow label={__("charge_tracker.content.csv_flavor")}>
                        <InputSelect
                            value={this.state.new_remote_upload_config.csv_delimiter.toString()}
                            onValue={v => this.setState({
                                new_remote_upload_config: {
                                    ...this.state.new_remote_upload_config,
                                    csv_delimiter: parseInt(v) as CSVFlavor
                                }
                            })}
                            items={[
                                [CSVFlavor.Excel.toString(), __("charge_tracker.content.csv_flavor_excel")],
                                [CSVFlavor.RFC4180.toString(), __("charge_tracker.content.csv_flavor_rfc4180")]
                            ]}
                        />
                    </FormRow>
                </div>
            </Collapse>
            <FormRow label={__("charge_tracker.content.target_user")}>
                <InputSelect
                    value={this.state.new_remote_upload_config.user_id.toString()}
                    placeholder={__("select")}
                    required
                    onValue={v => this.setState({
                        new_remote_upload_config: {
                            ...this.state.new_remote_upload_config,
                            user_id: parseInt(v)
                        }
                    })}
                    items={remote_access_user_items}
                />
            </FormRow>
        </>;
    }
//#endif

    async downloadCSVChargeLog(language: number, flavor: 'excel' | 'rfc4180', user_filter: number, start_minutes: number, end_minutes: number, price?: number) {
        const csvFlavorEnum = flavor === 'excel' ? 0 : 1; // CSVFlavor.Excel = 0, RFC4180 = 1

        const payload = {
            api_not_final_acked: true,
            language: language,
            start_timestamp_min: start_minutes,
            end_timestamp_min: end_minutes,
            user_filter: user_filter,
            csv_delimiter: csvFlavorEnum,
        };

        try {
            const csv = await API.call("charge_tracker/csv", payload, () => __("charge_tracker.script.download_charge_log_failed"), undefined, 2 * 60 * 1000);
            util.downloadToTimestampedFile(csv, language == Language.German ? "Ladelog" : "charge-log", "csv", flavor === 'excel' ? "text/csv; charset=windows-1252; header=present" : "text/csv; charset=utf-8; header=present");
        } catch (err) {
            util.add_alert("download-charge-log", "danger", () => __("charge_tracker.script.download_charge_log_failed"), err);
        }
    }

    override async isSaveAllowed(cfg: ChargeTrackerConfig) {
        return cfg.electricity_price == 0 || cfg.electricity_price >= 100;
    }

    render(props: {}, state: Readonly<ChargeTrackerState> & ChargeTrackerConfig) {
        if (!util.render_allowed())
            return <SubPage name="charge_tracker" />;

        let user_filter_items: [string, string][] = API.get('users/config').users.map(x => [x.id.toString(), (x.display_name == "Anonymous" && x.id == 0) ? __("charge_tracker.script.unknown_users") : x.display_name]);
        user_filter_items.unshift(["-1",  __("charge_tracker.script.deleted_users")]);
        user_filter_items.unshift(["-2", __("charge_tracker.script.all_users")]);

        // TODO show hint that day ahead prices are not used here!

        let dap_enabled = false;
//#if MODULE_DAY_AHEAD_PRICES_AVAILABLE
        dap_enabled = API.get('day_ahead_prices/config').enable;
//#endif
        let sendEmailComponent = <></>;
        let sendEmailDropdown = <></>;

        let history_modal_tag_name = "";
        if (state.history_modal_tag_id != "" && API.hasFeature("nfc")) {
            const tag = API.get("nfc/config").authorized_tags.find(t => t.tag_id == state.history_modal_tag_id && t.tag_type == state.history_modal_tag_type);
            history_modal_tag_name = tag?.name ?? "";
        }

        const history_modal_tag_text = state.history_modal_tag_id == ""
            ? __("charge_tracker.content.history_no_nfc")
            : history_modal_tag_name == ""
                ? state.history_modal_tag_id
                : `${history_modal_tag_name} (${state.history_modal_tag_id})`;
//#if MODULE_REMOTE_ACCESS_AVAILABLE
        sendEmailComponent = <>
                <FormSeparator heading={__("charge_tracker.content.charge_log_email_send_config")}/>
                <FormRow label={__("charge_tracker.content.charge_log_email_send_config")} help={__("charge_tracker.content.charge_log_email_send_config_help")}>
                    <Table
                        columnNames={[
                            __("charge_tracker.content.user_filter"),
                            __("charge_tracker.content.file_type"),
                            __("charge_tracker.content.target_user"),
                        ]}
                        rows={this.getRemoteUploadConfigTableRows(user_filter_items)}
                        addEnabled={state.remote_upload_configs.length < options.REMOTE_ACCESS_MAX_USERS}
                        addTitle={__("charge_tracker.content.charge_log_email_send_add_modal_title")}
                        addMessage={__("charge_tracker.content.charge_log_send_add_message")(state.remote_upload_configs.length, options.REMOTE_ACCESS_MAX_USERS)}
                        onAddShow={() => this.onAddRemoteUploadConfigShow()}
                        onAddGetChildren={() => this.onAddRemoteUploadConfigGetChildren(user_filter_items)}
                        onAddSubmit={() => this.onAddRemoteUploadConfigSubmit()}
                        addAdditionalFooterButtons={
                            <Button
                                variant="info"
                                disabled={this.state.new_remote_upload_config.user_id === 0 || this.state.generator_state != GenerationState.Ready}
                                onClick={async (e) => {
                                    e.preventDefault();
                                    await this.testSendChargeLog();
                                }}
                            >
                                <span class="me-2"><Send size={16}/></span>
                                {this.state.generator_state === GenerationState.ManualRemoteSend ? __("charge_tracker.content.sending") : __("charge_tracker.content.test_send")}
                                <Spinner animation="border" size="sm" as="span" className="ms-2" hidden={this.state.generator_state !== GenerationState.ManualRemoteSend}/>
                            </Button>
                        }
                    />
                </FormRow>
                <FormRow label={__("charge_tracker.content.next_send")}>
                    <InputText value={state.next_upload_timestamp_min == 0 ? __("charge_tracker.content.never") : util.timestamp_min_to_date(state.next_upload_timestamp_min)}/>
                </FormRow>
            </>

        const onDropdownClick = async (remote_access_user_uuid: string) => {
            try {
                let start_minutes = date_to_minutes(state.start_date, 'start_of_day');
                let end_minutes = date_to_minutes(state.end_date, 'end_of_day');

                await API.call("charge_tracker/send_charge_log", {
                    api_not_final_acked: true,
                    language: parseInt(state.language),
                    start_timestamp_min: start_minutes,
                    end_timestamp_min: end_minutes,
                    user_filter: parseInt(state.user_filter),
                    file_type: parseInt(state.file_type),
                    letterhead: state.pdf_letterhead,
                    csv_delimiter: state.file_type === "1" ? (state.csv_flavor === 'excel' ? 0 : 1) : 0,
                    cookie: Math.floor(Math.random() * 0xFFFFFFFF),
                    remote_access_user_uuid
                }, () => __("charge_tracker.script.upload_charge_log_failed"));
            } catch {}
        };
        const remoteAccessConfig = API.get('remote_access/config');
        const sendEmailDropdownItems = remoteAccessConfig.users.map((user) => {
            return <Dropdown.Item onClick={() => onDropdownClick(user.uuid)}>{user.email}</Dropdown.Item>
        });
        sendEmailDropdown = remoteAccessConfig.users.length > 0 ? <div class="col"><Dropdown>
            <Dropdown.Toggle className="text-nowrap w-100" disabled={state.generator_state !== GenerationState.Ready}>
                <span class="me-2">
                    {__("charge_tracker.content.charge_log_email_send_to_user")}
                </span>
                    <Mail />
                <Spinner animation="border" size="sm" className="ms-2" hidden={state.generator_state !== GenerationState.ManualRemoteSend}/>
            </Dropdown.Toggle>
            <Dropdown.Menu renderOnMount align="end">
                {sendEmailDropdownItems}
            </Dropdown.Menu>
        </Dropdown></div>
        : <></>;
//#endif

        return (
            <SubPage name="charge_tracker">
                <Modal size="xl" centered show={state.history_modal_charge != null} onHide={() => this.setState({history_modal_charge: null})}>
                    <Modal.Header closeButton>
                        <Modal.Title>{__("charge_tracker.content.history_title")}</Modal.Title>
                    </Modal.Header>
                    <Modal.Body>
                        <FormRow label={__("charge_tracker.content.history_nfc_tag")}>
                            <InputText value={history_modal_tag_text}/>
                        </FormRow>
                        <ChargeHistoryGraph samples={state.history_modal_samples} charge_start_timestamp_min={state.history_modal_charge?.timestamp_minutes ?? 0}/>
                    </Modal.Body>
                </Modal>

                <ConfigForm id="charge_tracker_config_form" title={__("charge_tracker.content.charge_tracker" )} isDirty={this.isDirty()} onSave={this.save} onDirtyChange={this.setDirty}>
                    <FormRow label={__("charge_tracker.content.price")} warning={__("charge_tracker.content.price_not_dynamic_yet")(dap_enabled)} show_warning>
                        <InputFloat class={state.electricity_price == 0 || state.electricity_price >= 100 ? "" : "is-invalid"} value={state.electricity_price} onValue={this.set('electricity_price')} digits={2} unit="ct/kWh" max={65535} min={0}/>
                        <div class="invalid-feedback">{__("charge_tracker.content.price_invalid")}</div>
                    </FormRow>
                </ConfigForm>

                {sendEmailComponent}

                <FormSeparator heading={__("charge_tracker.content.download")}/>

                <FormRow label={__("charge_tracker.content.user_filter")} label_muted={__("charge_tracker.content.user_filter_muted")}>
                    <InputSelect
                        value={state.user_filter}
                        onValue={(v) => this.setState({user_filter: v})}
                        items={user_filter_items ?? []}
                    />
                </FormRow>

                <FormRow label={__("charge_tracker.content.date_filter")} label_muted={__("charge_tracker.content.date_filter_muted")}>
                    <div class="row gx-0 gy-1">
                        <div class="col-md-6">
                            <div class="input-group">
                                <span class="input-group-text">{__("charge_tracker.content.from")}</span>
                                <InputDate
                                    className="rounded-end-md-0"
                                    date={state.start_date}
                                    onDate={(d: Date) => this.setState({start_date: d})}
                                />
                            </div>
                        </div>
                        <div class="col-md-6">
                            <div class="input-group">
                                <span class="input-group-text rounded-start-md-0 ms-md-n1px">{__("charge_tracker.content.to")}</span>
                                <InputDate
                                    date={state.end_date}
                                    onDate={(d: Date) => this.setState({end_date: d})}
                                />
                            </div>
                        </div>
                    </div>
                </FormRow>

                <FormRow label={__("charge_tracker.content.file_type")} label_muted={__("charge_tracker.content.file_type_muted")}>
                    <InputSelect
                        value={state.file_type}
                        onValue={this.set("file_type")}
                        items={[
                            ["0", __("charge_tracker.content.file_type_pdf")],
                            ["1", __("charge_tracker.content.file_type_csv")]
                        ]}
                    />
                </FormRow>

                <FormRow label={__("charge_tracker.content.language_label")}>
                    <InputSelect
                        value={state.language}
                        onValue={this.set("language")}
                        items={[
                            [Language.German.toString(), __("charge_tracker.content.language_german")],
                            [Language.English.toString(), __("charge_tracker.content.language_english")]
                        ]}
                    />
                </FormRow>

                <Collapse in={state.file_type == "0"}>
                    <div>
                        <FormRow label={__("charge_tracker.content.pdf_letterhead")} label_muted={__("charge_tracker.content.pdf_letterhead_muted")}>
                            <textarea name="letterhead" class="font-monospace form-control" id="letterhead" value={state.pdf_letterhead} onInput={(e) => {
                                let value = (e.target as HTMLInputElement).value;
                                if (new Blob([value]).size <= 512)
                                    this.setState({pdf_letterhead: value});
                                else
                                    this.setState({pdf_letterhead: state.pdf_letterhead});
                            }} cols={30} rows={6}/>
                        </FormRow>
                    </div>
                </Collapse>

                <Collapse in={state.file_type == "1"}>
                    <div>
                        <FormRow label={__("charge_tracker.content.csv_flavor")} label_muted={__("charge_tracker.content.csv_flavor_muted")}>
                            <InputSelect
                                value={state.csv_flavor}
                                onValue={(v) => this.setState({csv_flavor: v as any})}
                                items={[
                                    ["excel", __("charge_tracker.content.csv_flavor_excel")],
                                    ["rfc4180", __("charge_tracker.content.csv_flavor_rfc4180")]
                                ]}
                            />
                        </FormRow>
                    </div>
                </Collapse>

                <FormRow>
                    <div class="row g-2">
                        <div class="col">
                        <Button variant="primary" className="text-nowrap w-100" disabled={state.generator_state !== GenerationState.Ready} onClick={async () => {
                            this.setState({show_spinner: true});

                            let start_minutes = date_to_minutes(state.start_date, 'start_of_day');
                            let end_minutes = date_to_minutes(state.end_date, 'end_of_day');

                            try {
                                if (state.file_type === "0") {
                                    let pdf = await API.call("charge_tracker/pdf", {
                                        api_not_final_acked: true,
                                        language: parseInt(state.language),
                                        start_timestamp_min: start_minutes,
                                        end_timestamp_min: end_minutes,
                                        user_filter: parseInt(state.user_filter),
                                        letterhead: state.pdf_letterhead,
                                    }, () => __("charge_tracker.script.download_charge_log_failed"), undefined, 2 * 60 * 1000);
                                    util.downloadToTimestampedFile(pdf, parseInt(state.language) == Language.German ? "Ladelog" : "charge-log", "pdf", "application/pdf");
                                } else {
                                    // Download CSV
                                    await this.downloadCSVChargeLog(parseInt(state.language), state.csv_flavor, parseInt(state.user_filter), start_minutes, end_minutes, state.electricity_price);
                                }
                            } finally {
                                this.setState({show_spinner: false});
                            }
                        }}>
                            <span class="me-2">{__("charge_tracker.content.download_btn")}</span>
                            <Download/>
                            <Spinner animation="border" size="sm" as="span" className="ms-2" hidden={!state.show_spinner}/>
                        </Button>
                        </div>
                        {sendEmailDropdown}
                    </div>
                </FormRow>
                <FormSeparator heading={__("charge_tracker.content.tracked_charges")}/>

                <FormRow label={__("charge_tracker.content.tracked_charges")} label_muted={__("charge_tracker.content.tracked_charges_muted")}>
                    <InputText value={__("charge_tracker.script.tracked_charge_count")(state.tracked_charges, MAX_TRACKED_CHARGES)}/>
                </FormRow>

                <FormRow label={__("charge_tracker.content.first_charge_timestamp")} label_muted={__("charge_tracker.content.first_charge_timestamp_muted")}>
                    <InputText value={util.timestamp_min_to_date(state.first_charge_timestamp, __("charge_tracker.script.unknown_charge_start"))}/>
                </FormRow>

                <FormRow label={__("charge_tracker.content.remove")} label_muted={__("charge_tracker.content.remove_desc")}>
                    <Button variant="danger" className="w-100" onClick={async () => {
                        const modal = util.async_modal_ref.current;
                        if (!await modal.show({
                                title: () => __("charge_tracker.content.remove"),
                                body: () => __("charge_tracker.content.charge_tracker_remove_modal_text"),
                                no_text: () => __("charge_tracker.content.abort_remove"),
                                yes_text: () => __("charge_tracker.content.confirm_remove"),
                                no_variant: "secondary",
                                yes_variant: "danger"
                            }))
                            return;

                            await API.call('charge_tracker/remove_all_charges', {
                                    "do_i_know_what_i_am_doing": true
                                }, () => __("charge_tracker.script.remove_failed"));

                            util.postReboot(__("charge_tracker.script.remove_init"), __("util.reboot_text"));
                    }}>
                        {__("charge_tracker.content.remove_btn")}
                    </Button>
                </FormRow>

                <FormRow label={__("charge_tracker.content.last_charges")} label_muted={__("charge_tracker.content.last_charges_desc")}>
                    <ListGroup>
                        {this.get_last_charges(state.last_charges ?? [], state.electricity_price)}
                    </ListGroup>
                </FormRow>
            </SubPage>
        );
    }
}

export class ChargeTrackerStatus extends Component {
    render() {
        if (!util.render_allowed())
            return <StatusSection name="charge_tracker" />;

        let last_charges = API.get('charge_tracker/last_charges');
        let cc = API.get('charge_tracker/current_charge');
        let evse_uptime = API.get('evse/low_level_state').uptime;
        let energy_abs = API.get('meter/values').energy_abs;
        let users = API.get('users/config').users;
        let electricity_price = API.get('charge_tracker/config').electricity_price;

        let current_charge = <></>;

        if (cc.user_id != -1) {
            let charge_duration = evse_uptime - cc.evse_uptime_start
            if (evse_uptime < cc.evse_uptime_start)
                charge_duration += 0xFFFFFFFF;

            charge_duration = Math.floor(charge_duration / 1000);

            let charge: Charge = {
                charge_duration: charge_duration,
                energy_charged: (energy_abs === null || cc.meter_start === null) ? null : (energy_abs - cc.meter_start),
                timestamp_minutes: cc.timestamp_minutes,
                user_id: cc.user_id,
                dynamic_cost: cc.dynamic_cost
            };

            current_charge = <FormRow label={__("charge_tracker.status.current_charge")}>
                <ListGroup>
                    <TrackedCharge charge={charge}
                                    users={users}
                                    electricity_price={electricity_price}
                                    />
                </ListGroup>
            </FormRow>;
        }

        let last_charges_list = last_charges.length == 0 ? <></>
            : <FormRow label={__("charge_tracker.status.last_charges")}>
                <ListGroup>
                    {last_charges.slice(-3).map(c =>
                        <TrackedCharge charge={c}
                                        users={users}
                                        electricity_price={electricity_price}
                                        />
                    ).reverse()}
                </ListGroup>
            </FormRow>;

        return <StatusSection name="charge_tracker">
                {current_charge}
                {last_charges_list}
            </StatusSection>;
    }
}

export function pre_init() {
}

export function init() {
}

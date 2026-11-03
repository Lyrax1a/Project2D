`timescale 1ns/1ps
// SPDX-License-Identifier: MIT
// PB3 128-bit span DMA.  A logical request is split at the configured burst
// length and every 4 KiB boundary.  Read address bursts may be outstanding;
// data remains in-order because all traffic uses one AXI ID.  Write responses
// are intentionally serialized for the conservative pre-board baseline.
module axi128_master_pb3 #(
    parameter integer MAX_BURST_BEATS = 16,
    parameter integer OUTSTANDING_READS = 2,
    parameter integer OUTSTANDING_WRITES = 1,
    parameter integer TIMEOUT_CYCLES = 4096
) (
    input  wire         clk_i,
    input  wire         rst_n_i,
    input  wire         req_valid_i,
    output wire         req_ready_o,
    input  wire         req_write_i,
    input  wire [31:0]  req_addr_i,
    input  wire [23:0]  req_bytes_i,

    input  wire         wr_valid_i,
    output wire         wr_ready_o,
    input  wire [127:0] wr_data_i,
    input  wire [15:0]  wr_strb_i,
    output wire         rd_valid_o,
    input  wire         rd_ready_i,
    output wire [127:0] rd_data_o,
    output wire [15:0]  rd_strb_o,

    output reg          done_o,
    output reg          error_o,
    output reg          timeout_o,
    output reg  [31:0]  axi_read_bursts_o,
    output reg  [31:0]  axi_write_bursts_o,
    output reg  [31:0]  axi_read_beats_o,
    output reg  [31:0]  axi_write_beats_o,
    output reg  [31:0]  axi_useful_read_bytes_o,
    output reg  [31:0]  axi_useful_write_bytes_o,
    output reg  [31:0]  axi_bus_read_bytes_o,
    output reg  [31:0]  axi_bus_write_bytes_o,
    output reg  [31:0]  read_stall_cycles_o,
    output reg  [31:0]  write_stall_cycles_o,
    output reg  [31:0]  partial_write_beats_o,
    output reg  [31:0]  full_write_beats_o,
    output reg  [7:0]   max_read_outstanding_o,

    output reg  [31:0]  m_axi_awaddr,
    output reg  [7:0]   m_axi_awlen,
    output wire [2:0]   m_axi_awsize,
    output wire [1:0]   m_axi_awburst,
    output reg          m_axi_awvalid,
    input  wire         m_axi_awready,
    output wire [127:0] m_axi_wdata,
    output wire [15:0]  m_axi_wstrb,
    output wire         m_axi_wlast,
    output wire         m_axi_wvalid,
    input  wire         m_axi_wready,
    input  wire [1:0]   m_axi_bresp,
    input  wire         m_axi_bvalid,
    output wire         m_axi_bready,

    output reg  [31:0]  m_axi_araddr,
    output reg  [7:0]   m_axi_arlen,
    output wire [2:0]   m_axi_arsize,
    output wire [1:0]   m_axi_arburst,
    output reg          m_axi_arvalid,
    input  wire         m_axi_arready,
    input  wire [127:0] m_axi_rdata,
    input  wire [1:0]   m_axi_rresp,
    input  wire         m_axi_rlast,
    input  wire         m_axi_rvalid,
    output wire         m_axi_rready
);
    localparam [3:0] S_IDLE=0, S_READ=1, S_W_PLAN=2, S_W_AW=3,
                     S_W_DATA=4, S_W_RESP=5, S_FINISH=6;
    localparam integer READ_DEPTH = OUTSTANDING_READS < 1 ? 1 : OUTSTANDING_READS;
    localparam [24:0] MAX_BURST_BYTES = MAX_BURST_BEATS * 16;

    reg [3:0] state_q;
    reg [31:0] timeout_count_q;
    reg [3:0] request_head_q;
    reg [3:0] request_tail_q;
    reg [24:0] request_total_beats_q;
    reg [24:0] read_receive_index_q;

    reg [31:0] read_issue_addr_q;
    reg [24:0] read_issue_beats_q;
    reg [8:0] read_desc_beats [0:READ_DEPTH-1];
    reg [7:0] read_desc_head_q, read_desc_tail_q, read_outstanding_q;
    reg [8:0] read_response_index_q;

    reg [31:0] write_logical_addr_q;
    reg [23:0] write_remaining_q, write_payload_q;
    reg [8:0] write_burst_beats_q, write_beat_index_q;
    reg [3:0] write_head_q, write_tail_q;

    reg [24:0] plan_capacity_d, plan_boundary_d, plan_payload_d, plan_beats_d;
    reg [24:0] read_boundary_beats_d, read_next_beats_d;
    reg [15:0] read_mask_d, write_range_mask_d;
    reg progress_d;
    integer popcount_index;
    reg [5:0] read_useful_count_d, write_useful_count_d;

    wire address_overflow = ({1'b0, req_addr_i} + {9'b0, req_bytes_i}) > 33'h1_0000_0000;
    wire read_push = m_axi_arvalid && m_axi_arready;
    wire read_pop = m_axi_rvalid && m_axi_rready && m_axi_rlast;
    wire [15:0] effective_write_strb = write_range_mask_d & wr_strb_i;

    assign req_ready_o = state_q == S_IDLE;
    assign m_axi_awsize = 3'd4;
    assign m_axi_arsize = 3'd4;
    assign m_axi_awburst = 2'b01;
    assign m_axi_arburst = 2'b01;
    assign m_axi_wdata = wr_data_i;
    assign m_axi_wstrb = effective_write_strb;
    assign m_axi_wlast = write_beat_index_q == write_burst_beats_q - 1'b1;
    assign m_axi_wvalid = state_q == S_W_DATA && wr_valid_i;
    assign wr_ready_o = state_q == S_W_DATA && m_axi_wready;
    assign m_axi_bready = state_q == S_W_RESP;
    assign rd_valid_o = state_q == S_READ && m_axi_rvalid;
    assign rd_data_o = m_axi_rdata;
    assign rd_strb_o = read_mask_d;
    assign m_axi_rready = state_q == S_READ && rd_ready_i;

    always @* begin
        plan_capacity_d = MAX_BURST_BYTES - write_logical_addr_q[3:0];
        plan_boundary_d = 25'd4096 - {13'b0, write_logical_addr_q[11:0]};
        plan_payload_d = write_remaining_q;
        if (plan_payload_d > plan_capacity_d)
            plan_payload_d = plan_capacity_d;
        if (plan_payload_d > plan_boundary_d)
            plan_payload_d = plan_boundary_d;
        plan_beats_d = ({21'b0, write_logical_addr_q[3:0]} + plan_payload_d + 15) >> 4;

        read_boundary_beats_d = (25'd4096 - {13'b0, read_issue_addr_q[11:0]}) >> 4;
        read_next_beats_d = read_issue_beats_q;
        if (read_next_beats_d > MAX_BURST_BEATS)
            read_next_beats_d = MAX_BURST_BEATS;
        if (read_next_beats_d > read_boundary_beats_d)
            read_next_beats_d = read_boundary_beats_d;

        read_mask_d = 16'hffff;
        if (read_receive_index_q == 0)
            read_mask_d = read_mask_d & (16'hffff << request_head_q);
        if (read_receive_index_q == request_total_beats_q - 1'b1)
            read_mask_d = read_mask_d & (request_tail_q == 0 ? 16'hffff :
                          ((16'h0001 << request_tail_q) - 1'b1));

        write_range_mask_d = 16'hffff;
        if (write_beat_index_q == 0)
            write_range_mask_d = write_range_mask_d & (16'hffff << write_head_q);
        if (write_beat_index_q == write_burst_beats_q - 1'b1)
            write_range_mask_d = write_range_mask_d & (write_tail_q == 0 ? 16'hffff :
                                 ((16'h0001 << write_tail_q) - 1'b1));

        read_useful_count_d = 0;
        write_useful_count_d = 0;
        for (popcount_index = 0; popcount_index < 16; popcount_index = popcount_index + 1) begin
            read_useful_count_d = read_useful_count_d + read_mask_d[popcount_index];
            write_useful_count_d = write_useful_count_d + effective_write_strb[popcount_index];
        end

        progress_d = (m_axi_arvalid && m_axi_arready) ||
                     (m_axi_rvalid && m_axi_rready) ||
                     (m_axi_awvalid && m_axi_awready) ||
                     (m_axi_wvalid && m_axi_wready) ||
                     (m_axi_bvalid && m_axi_bready);
    end

    always @(posedge clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin
            state_q <= S_IDLE;
            timeout_count_q <= 0;
            request_head_q <= 0; request_tail_q <= 0;
            request_total_beats_q <= 0; read_receive_index_q <= 0;
            read_issue_addr_q <= 0; read_issue_beats_q <= 0;
            read_desc_head_q <= 0; read_desc_tail_q <= 0; read_outstanding_q <= 0;
            read_response_index_q <= 0;
            write_logical_addr_q <= 0; write_remaining_q <= 0; write_payload_q <= 0;
            write_burst_beats_q <= 0; write_beat_index_q <= 0;
            write_head_q <= 0; write_tail_q <= 0;
            done_o <= 0; error_o <= 0; timeout_o <= 0;
            axi_read_bursts_o <= 0; axi_write_bursts_o <= 0;
            axi_read_beats_o <= 0; axi_write_beats_o <= 0;
            axi_useful_read_bytes_o <= 0; axi_useful_write_bytes_o <= 0;
            axi_bus_read_bytes_o <= 0; axi_bus_write_bytes_o <= 0;
            read_stall_cycles_o <= 0; write_stall_cycles_o <= 0;
            partial_write_beats_o <= 0; full_write_beats_o <= 0;
            max_read_outstanding_o <= 0;
            m_axi_awaddr <= 0; m_axi_awlen <= 0; m_axi_awvalid <= 0;
            m_axi_araddr <= 0; m_axi_arlen <= 0; m_axi_arvalid <= 0;
        end else begin
            done_o <= 1'b0;
            if (state_q == S_IDLE) begin
                timeout_count_q <= 0;
                if (req_valid_i) begin
                    error_o <= req_bytes_i == 0 || address_overflow ||
                               MAX_BURST_BEATS < 1 || MAX_BURST_BEATS > 256 ||
                               OUTSTANDING_READS < 1 || OUTSTANDING_WRITES < 1;
                    timeout_o <= 1'b0;
                    request_head_q <= req_addr_i[3:0];
                    request_tail_q <= (req_addr_i[3:0] + req_bytes_i[3:0]) & 4'hf;
                    request_total_beats_q <= ({21'b0, req_addr_i[3:0]} + req_bytes_i + 15) >> 4;
                    if (req_bytes_i == 0 || address_overflow || MAX_BURST_BEATS < 1 ||
                        MAX_BURST_BEATS > 256 || OUTSTANDING_READS < 1 || OUTSTANDING_WRITES < 1)
                        state_q <= S_FINISH;
                    else if (req_write_i) begin
                        write_logical_addr_q <= req_addr_i;
                        write_remaining_q <= req_bytes_i;
                        state_q <= S_W_PLAN;
                    end else begin
                        read_issue_addr_q <= {req_addr_i[31:4], 4'b0};
                        read_issue_beats_q <= ({21'b0, req_addr_i[3:0]} + req_bytes_i + 15) >> 4;
                        read_receive_index_q <= 0;
                        read_desc_head_q <= 0; read_desc_tail_q <= 0;
                        read_outstanding_q <= 0; read_response_index_q <= 0;
                        state_q <= S_READ;
                    end
                end
            end else begin
                if (progress_d)
                    timeout_count_q <= 0;
                else if (timeout_count_q < TIMEOUT_CYCLES)
                    timeout_count_q <= timeout_count_q + 1'b1;
                if (!progress_d && timeout_count_q == TIMEOUT_CYCLES - 1) begin
                    timeout_o <= 1'b1;
                    error_o <= 1'b1;
                    m_axi_awvalid <= 1'b0;
                    m_axi_arvalid <= 1'b0;
                    state_q <= S_FINISH;
                end else case (state_q)
                    S_READ: begin
                        if (!m_axi_arvalid &&
                            read_issue_beats_q != 0 &&
                            read_outstanding_q < OUTSTANDING_READS) begin
                            m_axi_araddr <= read_issue_addr_q;
                            m_axi_arlen <= read_next_beats_d[7:0] - 1'b1;
                            m_axi_arvalid <= 1'b1;
                        end else if (read_push)
                            m_axi_arvalid <= 1'b0;

                        if (read_push) begin
                            read_desc_beats[read_desc_tail_q] <= {1'b0, m_axi_arlen} + 1'b1;
                            read_desc_tail_q <= read_desc_tail_q == READ_DEPTH-1 ? 0 : read_desc_tail_q + 1'b1;
                            read_issue_addr_q <= read_issue_addr_q + ({24'b0, m_axi_arlen} + 1'b1) * 16;
                            read_issue_beats_q <= read_issue_beats_q - ({24'b0, m_axi_arlen} + 1'b1);
                            axi_read_bursts_o <= axi_read_bursts_o + 1'b1;
                        end
                        case ({read_push, read_pop})
                            2'b10: read_outstanding_q <= read_outstanding_q + 1'b1;
                            2'b01: read_outstanding_q <= read_outstanding_q - 1'b1;
                            default: read_outstanding_q <= read_outstanding_q;
                        endcase
                        if (read_outstanding_q + (read_push ? 1'b1 : 1'b0) > max_read_outstanding_o)
                            max_read_outstanding_o <= read_outstanding_q + (read_push ? 1'b1 : 1'b0);

                        if (m_axi_rvalid && m_axi_rready) begin
                            axi_read_beats_o <= axi_read_beats_o + 1'b1;
                            axi_bus_read_bytes_o <= axi_bus_read_bytes_o + 16;
                            axi_useful_read_bytes_o <= axi_useful_read_bytes_o + read_useful_count_d;
                            if (m_axi_rresp != 2'b00 ||
                                m_axi_rlast != (read_response_index_q ==
                                                read_desc_beats[read_desc_head_q] - 1'b1))
                                error_o <= 1'b1;
                            read_receive_index_q <= read_receive_index_q + 1'b1;
                            if (m_axi_rlast) begin
                                read_desc_head_q <= read_desc_head_q == READ_DEPTH-1 ? 0 : read_desc_head_q + 1'b1;
                                read_response_index_q <= 0;
                                if (read_issue_beats_q == 0 && !m_axi_arvalid && !read_push &&
                                    read_outstanding_q == 1)
                                    state_q <= S_FINISH;
                            end else
                                read_response_index_q <= read_response_index_q + 1'b1;
                        end
                        if ((m_axi_arvalid && !m_axi_arready) ||
                            (read_outstanding_q != 0 && (!m_axi_rvalid || !rd_ready_i)))
                            read_stall_cycles_o <= read_stall_cycles_o + 1'b1;
                    end
                    S_W_PLAN: begin
                        write_payload_q <= plan_payload_d[23:0];
                        write_burst_beats_q <= plan_beats_d[8:0];
                        write_beat_index_q <= 0;
                        write_head_q <= write_logical_addr_q[3:0];
                        write_tail_q <= (write_logical_addr_q[3:0] + plan_payload_d[3:0]) & 4'hf;
                        m_axi_awaddr <= {write_logical_addr_q[31:4], 4'b0};
                        m_axi_awlen <= plan_beats_d[7:0] - 1'b1;
                        m_axi_awvalid <= 1'b1;
                        state_q <= S_W_AW;
                    end
                    S_W_AW: begin
                        if (m_axi_awvalid && m_axi_awready) begin
                            m_axi_awvalid <= 1'b0;
                            axi_write_bursts_o <= axi_write_bursts_o + 1'b1;
                            state_q <= S_W_DATA;
                        end else
                            write_stall_cycles_o <= write_stall_cycles_o + 1'b1;
                    end
                    S_W_DATA: begin
                        if (m_axi_wvalid && m_axi_wready) begin
                            axi_write_beats_o <= axi_write_beats_o + 1'b1;
                            axi_bus_write_bytes_o <= axi_bus_write_bytes_o + 16;
                            axi_useful_write_bytes_o <= axi_useful_write_bytes_o + write_useful_count_d;
                            if (effective_write_strb == 16'hffff)
                                full_write_beats_o <= full_write_beats_o + 1'b1;
                            else
                                partial_write_beats_o <= partial_write_beats_o + 1'b1;
                            if (m_axi_wlast)
                                state_q <= S_W_RESP;
                            else
                                write_beat_index_q <= write_beat_index_q + 1'b1;
                        end else if (!m_axi_wready || !wr_valid_i)
                            write_stall_cycles_o <= write_stall_cycles_o + 1'b1;
                    end
                    S_W_RESP: begin
                        if (m_axi_bvalid) begin
                            if (m_axi_bresp != 2'b00)
                                error_o <= 1'b1;
                            if (write_remaining_q == write_payload_q)
                                state_q <= S_FINISH;
                            else begin
                                write_logical_addr_q <= write_logical_addr_q + write_payload_q;
                                write_remaining_q <= write_remaining_q - write_payload_q;
                                state_q <= S_W_PLAN;
                            end
                        end else
                            write_stall_cycles_o <= write_stall_cycles_o + 1'b1;
                    end
                    S_FINISH: begin
                        done_o <= 1'b1;
                        m_axi_arvalid <= 1'b0;
                        m_axi_awvalid <= 1'b0;
                        state_q <= S_IDLE;
                    end
                    default: begin error_o <= 1'b1; state_q <= S_FINISH; end
                endcase
            end
        end
    end
endmodule

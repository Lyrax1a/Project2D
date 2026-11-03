`timescale 1ns/1ps
// SPDX-License-Identifier: MIT
// PB3 frame renderer: graphics semantics are unchanged from PB2, while every
// clipped row is transferred as packed 128-bit AXI spans.  The pixel stage is
// deliberately scalar; beat RAMs provide the unpack/compute/pack decoupling.
module frame_renderer_pb3 #(
    parameter integer COMMAND_ADDR_WIDTH = 10,
    parameter integer NARROW_DIRECT_PIXELS = 0,
    parameter integer OPTIMIZE_SOURCE_ROWS = 1,
    parameter integer OPTIMIZE_TILE_IO = 1,
    parameter integer PIPELINED_TILE_PIXELS = 1,
    parameter integer FILTER_TILE_GROUPS = 1,
    parameter integer STREAM_TRANSFERS = 1,
    parameter integer FAST_BLOCK_COPY = 1,
    parameter integer TILE_SIZE = 64,
    parameter integer MAX_ROW_PIXELS = 1280,
    parameter integer MAX_BURST_BEATS = 16,
    parameter integer OUTSTANDING_READS = 2,
    parameter integer OUTSTANDING_WRITES = 1
) (
    input wire clk_i, input wire rst_n_i,
    input wire cmd_we_i,
    input wire [COMMAND_ADDR_WIDTH-1:0] cmd_addr_i,
    input wire [255:0] cmd_wdata_i,
    input wire start_i, input wire tile_mode_i,
    input wire [15:0] frame_width_i, input wire [15:0] frame_height_i,
    input wire [31:0] framebuffer_base_i,
    input wire [15:0] framebuffer_stride_i,
    input wire [COMMAND_ADDR_WIDTH:0] command_count_i,
    output reg busy_o, output reg done_o, output reg error_o,
    output reg [31:0] axi_read_bursts_o, output reg [31:0] axi_write_bursts_o,
    output reg [31:0] axi_read_beats_o, output reg [31:0] axi_write_beats_o,
    output reg [31:0] axi_useful_read_bytes_o,
    output reg [31:0] axi_useful_write_bytes_o,
    output reg [31:0] axi_bus_read_bytes_o, output reg [31:0] axi_bus_write_bytes_o,
    output reg [31:0] read_stall_cycles_o, output reg [31:0] write_stall_cycles_o,
    output reg [31:0] partial_write_beats_o, output reg [31:0] full_write_beats_o,
    output reg [31:0] source_cache_hits_o,
    output reg [31:0] source_cache_misses_o,
    output reg [31:0] source_cache_refill_bytes_o,
    output reg [31:0] perf_pixels_o,
    output wire [7:0] max_read_outstanding_o,
    output wire [31:0] debug_signature_o,

    output wire [31:0] m_axi_awaddr, output wire [7:0] m_axi_awlen,
    output wire [2:0] m_axi_awsize, output wire [1:0] m_axi_awburst,
    output wire m_axi_awvalid, input wire m_axi_awready,
    output wire [127:0] m_axi_wdata, output wire [15:0] m_axi_wstrb,
    output wire m_axi_wlast, output wire m_axi_wvalid, input wire m_axi_wready,
    input wire [1:0] m_axi_bresp, input wire m_axi_bvalid, output wire m_axi_bready,
    output wire [31:0] m_axi_araddr, output wire [7:0] m_axi_arlen,
    output wire [2:0] m_axi_arsize, output wire [1:0] m_axi_arburst,
    output wire m_axi_arvalid, input wire m_axi_arready,
    input wire [127:0] m_axi_rdata, input wire [1:0] m_axi_rresp,
    input wire m_axi_rlast, input wire m_axi_rvalid, output wire m_axi_rready
);
    localparam integer MAX_ROW_BEATS = (MAX_ROW_PIXELS + 14) / 8;
    localparam integer GROUP_SHIFT = COMMAND_ADDR_WIDTH<4 ? COMMAND_ADDR_WIDTH : 4;
    localparam integer GROUP_COMMANDS = 1<<GROUP_SHIFT;
    localparam integer GROUP_COUNT = (1<<COMMAND_ADDR_WIDTH)/GROUP_COMMANDS;
    localparam integer SOURCE_CACHE_BYTES = 8192;
    localparam integer CACHE_BLOCK_BYTES = 256;
    localparam integer CACHE_BLOCK_BEATS = CACHE_BLOCK_BYTES / 16;
    localparam integer CACHE_BLOCKS = SOURCE_CACHE_BYTES / CACHE_BLOCK_BYTES;
    localparam [7:0] OP_NOP=8'h00, OP_FILL=8'h01, OP_OPAQUE=8'h02,
                     OP_KEY=8'h03, OP_ALPHA=8'h04, OP_PIXEL=8'h05, OP_MASK=8'h06, OP_TRIANGLE=8'h07, OP_CLEAR=8'h7e, OP_END=8'h7f;
    localparam [6:0] ST_IDLE=0, ST_CMD_ADDR=1, ST_CMD_FETCH=2, ST_DECODE=3,
        ST_ROW_SETUP=4, ST_SRC_REQ=5, ST_SRC_READ=6, ST_DST_REQ=7, ST_DST_READ=8,
        ST_PROCESS_INIT=9, ST_PROCESS_IMM=10, ST_TILE_PIXEL_ADDR=11,
        ST_TILE_PIXEL_WAIT=12, ST_TILE_PIXEL_APPLY=13, ST_WRITE_REQ=14,
        ST_WRITE_STREAM=15, ST_NEXT_ROW=16, ST_NEXT_COMMAND=17,
        ST_PROCESS_WAIT=18, ST_TILE_UNPACK_WAIT=19,
        ST_TILE_SETUP=20, ST_TILE_LOAD_REQ=21, ST_TILE_LOAD_READ=22,
        ST_TILE_UNPACK_INIT=23, ST_TILE_UNPACK=24,
        ST_TILE_FLUSH_INIT=25, ST_TILE_FLUSH_ADDR=26, ST_TILE_FLUSH_WAIT=27,
        ST_TILE_FLUSH_PACK=28, ST_TILE_FLUSH_WRITE_REQ=29,
        ST_TILE_FLUSH_WRITE=30, ST_TILE_NEXT=31, ST_COMPLETE=40,
        ST_PROCESS_CAPTURE=41, ST_SRC_CACHE_ADDR=42,
        ST_SRC_CACHE_WAIT=43, ST_SRC_CACHE_CHECK=44,
        ST_SRC_CACHE_MISS_REQ=45, ST_SRC_CACHE_MISS_READ=46,
        ST_CACHE_INVALIDATE_IMM=47, ST_CACHE_INVALIDATE_TILE=48,
        ST_SOURCE_CLASSIFY=49, ST_TILE_PIPE=50,
        ST_GROUP_ADDR=51, ST_GROUP_CHECK=52, ST_VECTOR_DRAIN=53, ST_TRI_DESC_REQ=54, ST_TRI_DESC_READ=55,
        ST_TRI_ROW_START=56, ST_TRI_ROW_RUN=57;

    (* ram_style="block" *) reg [255:0] command_ram [0:(1<<COMMAND_ADDR_WIDTH)-1];
    // This shallow table wastes five RAM10 blocks; use available logic instead.
    (* syn_ramstyle="registers" *) reg [73:0] group_ram [0:GROUP_COUNT-1];
    (* ram_style="block" *) reg [15:0] tile_ram [0:TILE_SIZE*TILE_SIZE-1];
    (* ram_style="block" *) reg [127:0] src_beat_ram [0:MAX_ROW_BEATS-1];
    (* ram_style="block" *) reg [127:0] dst_beat_ram [0:MAX_ROW_BEATS-1];
    (* ram_style="block" *) reg [127:0] out_data_ram [0:MAX_ROW_BEATS-1];
    (* ram_style="block" *) reg [15:0] out_strb_ram [0:MAX_ROW_BEATS-1];
    (* ram_style="block" *) reg [127:0] source_cache_data [0:SOURCE_CACHE_BYTES/16-1];
    reg [18:0] source_cache_tag [0:CACHE_BLOCKS-1];
    reg source_cache_valid [0:CACHE_BLOCKS-1];

    reg [255:0] command_read_q, command_q;
    reg [73:0] group_read_q,group_accum_q,group_accum_d;
    reg building_groups_q;
    reg [15:0] tile_read_q;
    reg [11:0] tile_addr_q;
    reg [11:0] tile_write_addr_q;
    reg tile_we_q;
    reg [15:0] tile_wdata_q;
    reg [6:0] state_q;
    reg mode_tile_q;
    reg [15:0] frame_width_q, frame_height_q, framebuffer_stride_q;
    reg [31:0] framebuffer_base_q;
    reg [COMMAND_ADDR_WIDTH:0] command_count_q, command_index_q;
    reg [7:0] opcode_q, flags_q, alpha_q;
    reg [31:0] src_base_q, dst_base_q, parameter_q;
    reg signed [15:0] src_x_q, src_y_q, dst_x_q, dst_y_q;
    reg [15:0] width_q, height_q, src_stride_q, dst_stride_q;
    reg [15:0] clip_x0_q, clip_y0_q, clip_x1_q, clip_y1_q, row_y_q;
    reg [15:0] row_pixels_q, process_index_q;
    reg [15:0] tile_x_q, tile_y_q, tile_width_q, tile_height_q, tile_row_q;
    reg [31:0] source_row_addr_q, destination_row_addr_q;
    reg [3:0] source_head_q, destination_head_q;
    reg [15:0] read_beat_index_q, write_beat_index_q, out_beat_count_q;
    reg [127:0] pack_data_q;
    reg [15:0] pack_strb_q;
    reg [15:0] src_read_addr_q, dst_read_addr_q, out_read_addr_q;
    reg [127:0] src_beat_read_q, src_beat_next_read_q, dst_beat_read_q, out_data_read_q;
    reg [15:0] out_strb_read_q;
    reg write_data_valid_q;
    reg [15:0] source_pixel_q, destination_pixel_q;
    reg scalar_mask_format_q, triangle_q, triangle_loaded_q;
    reg [511:0] triangle_descriptor_q;
    wire triangle_ready,triangle_valid,triangle_last,triangle_done,triangle_error;
    wire [127:0] triangle_data;
    wire [3:0] triangle_index;
    ts2d_triangle_row triangle_row(
        .clk(clk_i),.rst_n(rst_n_i),.valid_i(state_q==ST_TRI_ROW_START),
        .ready_o(triangle_ready),.descriptor_i(triangle_descriptor_q),
        .inclusive_i(parameter_q[18:16]),.x_i(source_min_x_d[7:0]),
        .y_i(source_y_d[7:0]),.count_i(row_pixels_q[7:0]),
        .valid_o(triangle_valid),.ready_i(state_q==ST_TRI_ROW_RUN),
        .data_o(triangle_data),.index_o(triangle_index),.last_o(triangle_last),
        .done_o(triangle_done),.error_o(triangle_error));
    reg [8:0] source_cache_read_addr_q;
    reg [127:0] source_cache_read_q;
    reg [18:0] source_cache_tag_read_q;
    reg source_cache_valid_read_q;
    reg [31:0] source_cache_fill_addr_q;
    reg [4:0] source_cache_fill_beat_q;
    reg [31:0] source_cache_invalidate_addr_q;
    integer source_cache_index;
    reg row_has_write_q, row_needs_destination_q;
    reg tile_loaded_q;
    reg [TILE_SIZE-1:0] tile_dirty_rows_q;
    reg [15:0] pipe_issue_q,pipe_read_index_q,pipe_apply_index_q;
    reg pipe_read_valid_q,pipe_apply_valid_q;
    reg beat_has_write_d, beat_needs_destination_d;
    reg [15:0] classify_pixel_d;
    integer classify_lane, classify_offset;

    reg [31:0] base_read_bursts_q, base_write_bursts_q;
    reg [31:0] base_read_beats_q, base_write_beats_q;
    reg [31:0] base_useful_read_q, base_useful_write_q;
    reg [31:0] base_bus_read_q, base_bus_write_q;
    reg [31:0] base_read_stall_q, base_write_stall_q;
    reg [31:0] base_partial_q, base_full_q;

    wire [7:0] cmd_opcode = command_q[7:0];
    wire [7:0] cmd_flags = command_q[15:8];
    wire [7:0] cmd_alpha = command_q[31:24];
    wire [31:0] cmd_src_base = command_q[63:32];
    wire [31:0] cmd_dst_base = command_q[95:64];
    wire signed [15:0] cmd_src_x = command_q[111:96];
    wire signed [15:0] cmd_src_y = command_q[127:112];
    wire signed [15:0] cmd_dst_x = command_q[143:128];
    wire signed [15:0] cmd_dst_y = command_q[159:144];
    wire [15:0] cmd_width = command_q[175:160];
    wire [15:0] cmd_height = command_q[191:176];
    wire [15:0] cmd_src_stride = command_q[207:192];
    wire [15:0] cmd_dst_stride = command_q[223:208];
    wire [31:0] cmd_parameter = command_q[255:224];
    wire command_invalid =
        (cmd_opcode!=OP_FILL && cmd_opcode!=OP_CLEAR && cmd_opcode!=OP_OPAQUE &&
         cmd_opcode!=OP_KEY && cmd_opcode!=OP_ALPHA && cmd_opcode!=OP_PIXEL && cmd_opcode!=OP_MASK && cmd_opcode!=OP_TRIANGLE) || !cmd_flags[0] ||
        (cmd_opcode==OP_PIXEL && (command_q[23:16]!=8'd1 || !cmd_flags[3])) ||
        (cmd_opcode==OP_MASK && (command_q[23:16]!=8'd2 || !cmd_flags[3])) ||
        (cmd_opcode==OP_TRIANGLE && (command_q[23:16]!=8'd3 || cmd_flags!=8'd9 ||
         cmd_alpha!=255 || cmd_src_base[5:0]!=0 || cmd_src_base[31:28]!=0 ||
         cmd_src_stride!=0 || cmd_src_x<0 || cmd_src_y<0 ||
         $unsigned(cmd_src_x)+{1'b0,cmd_width}>128 ||
         $unsigned(cmd_src_y)+{1'b0,cmd_height}>128 ||
         cmd_parameter[31:19]!=0)) ||
        cmd_width==0 || cmd_height==0 || cmd_dst_stride[0] || cmd_dst_base[0] ||
        cmd_dst_base!=framebuffer_base_q || cmd_dst_stride!=framebuffer_stride_q ||
        cmd_dst_stride<frame_width_q*2 ||
        ((cmd_opcode!=OP_FILL && cmd_opcode!=OP_CLEAR && cmd_opcode!=OP_TRIANGLE) &&
         (cmd_src_stride[0] || cmd_src_base[0] || cmd_src_stride<cmd_width*2));
    wire group_end = (command_index_q & (GROUP_COMMANDS-1))==GROUP_COMMANDS-1;
    wire [COMMAND_ADDR_WIDTH:0] next_group_index =
        (command_index_q & ~(GROUP_COMMANDS-1))+GROUP_COMMANDS;

    reg signed [17:0] dx0_d, dy0_d, dx1_d, dy1_d;
    reg signed [17:0] bound_x0_d, bound_y0_d, bound_x1_d, bound_y1_d;
    reg signed [17:0] ix0_d, iy0_d, ix1_d, iy1_d;
    reg signed [17:0] local_y_d, source_y_d, source_min_x_d;
    reg [31:0] source_row_addr_d, destination_row_addr_d;
    reg [15:0] src_linear_d, dst_linear_d;
    reg [15:0] source_pixel_d, destination_pixel_d, render_pixel_d;
    reg render_write_d;
    reg [127:0] pack_data_d;
    reg [15:0] pack_strb_d;
    reg [2:0] pack_lane_d;
    reg [11:0] process_tile_addr_d;
    wire [15:0] unpack_linear = source_head_q[3:1] + process_index_q;
    wire [2:0] flush_lane = (destination_head_q[3:1] + process_index_q) & 7;
    wire [31:0] source_cache_beat_addr = {source_row_addr_q[31:4],4'b0} +
                                              (read_beat_index_q << 4);
    wire [16:0] source_span_bytes = {1'b0,source_head_q} +
                                    ({1'b0,row_pixels_q} << 1);
    wire [15:0] source_span_beats = (source_span_bytes + 15) >> 4;
    wire [31:0] immediate_write_end_addr = destination_row_addr_q +
                                               (row_pixels_q << 1) - 1'b1;
    wire [31:0] tile_write_end_addr = destination_row_addr_q +
                                      (tile_width_q << 1) - 1'b1;

    function automatic [7:0] expand5(input [4:0] value_i);
        expand5 = {value_i, value_i[4:2]};
    endfunction
    function automatic [7:0] expand6(input [5:0] value_i);
        expand6 = {value_i, value_i[5:4]};
    endfunction
    function automatic [7:0] blend_channel(
        input [7:0] source_i, input [7:0] destination_i, input [7:0] alpha_i_f
    );
        reg [16:0] total, biased, corrected;
        begin
            if (alpha_i_f == 0) blend_channel = destination_i;
            else if (alpha_i_f == 255) blend_channel = source_i;
            else begin
                total = source_i * alpha_i_f + destination_i * (8'd255-alpha_i_f);
                biased = total + 17'd128;
                corrected = biased + (biased >> 8);
                blend_channel = corrected[15:8];
            end
        end
    endfunction
    function automatic [15:0] alpha_pixel(
        input [15:0] source_i, input [15:0] destination_i, input [7:0] alpha_i_f
    );
        reg [7:0] red, green, blue;
        begin
            if (alpha_i_f == 0) alpha_pixel = destination_i;
            else if (alpha_i_f == 255) alpha_pixel = source_i;
            else begin
                red = blend_channel(expand5(source_i[15:11]), expand5(destination_i[15:11]), alpha_i_f);
                green = blend_channel(expand6(source_i[10:5]), expand6(destination_i[10:5]), alpha_i_f);
                blue = blend_channel(expand5(source_i[4:0]), expand5(destination_i[4:0]), alpha_i_f);
                alpha_pixel = {red[7:3], green[7:2], blue[7:3]};
            end
        end
    endfunction
    function automatic [15:0] beat_pixel(input [127:0] beat_i,input [2:0] lane_i);
        reg [127:0] shifted;
        begin shifted=beat_i>>(lane_i*16);beat_pixel=shifted[15:0];end
    endfunction
    function automatic [15:0] alpha_argb(input [15:0] s,input [15:0] d,input [7:0] a,input mask_format,input [15:0] color);
        reg [16:0] product,biased;reg [7:0] opacity,r,g,b;
        begin
            product=(mask_format ? s[7:0] : {s[15:12],s[15:12]})*a+17'd128;
            biased=product+(product>>8);opacity=biased[15:8];
            r=blend_channel((mask_format ? expand5(color[15:11]) : {s[11:8],s[11:8]}),expand5(d[15:11]),opacity);
            g=blend_channel((mask_format ? expand6(color[10:5]) : {s[7:4],s[7:4]}),expand6(d[10:5]),opacity);
            b=blend_channel((mask_format ? expand5(color[4:0]) : {s[3:0],s[3:0]}),expand5(d[4:0]),opacity);
            alpha_argb={r[7:3],g[7:2],b[7:3]};
        end
    endfunction
    wire is_alpha = opcode_q==OP_ALPHA || opcode_q==OP_PIXEL || opcode_q==OP_MASK;
    wire classify_source = OPTIMIZE_SOURCE_ROWS && (is_alpha || opcode_q==OP_KEY);
    wire [15:0] unpack_pixel = beat_pixel(src_beat_read_q,unpack_linear[2:0]);
    wire [15:0] next_src_linear = source_head_q[3:1]+
        (flags_q[1] ? row_pixels_q-2-process_index_q : process_index_q+1);
    wire [15:0] next_dst_linear = destination_head_q[3:1]+process_index_q+1;
    wire tile_pipe_active = PIPELINED_TILE_PIXELS && state_q==ST_TILE_PIPE;
    wire fast_copy_active = FAST_BLOCK_COPY && !mode_tile_q && opcode_q==OP_OPAQUE && !flags_q[1];
    wire vector_active = FAST_BLOCK_COPY && !mode_tile_q && !flags_q[1] &&
        (opcode_q==OP_KEY || opcode_q==OP_ALPHA || opcode_q==OP_PIXEL || opcode_q==OP_MASK);
    wire vector_issue = vector_active && state_q==ST_PROCESS_IMM;
    wire copy_prefetch = (fast_copy_active || vector_active) && state_q==ST_PROCESS_IMM;
    // A destination beat may straddle two source beats. Two registered reads
    // retain one output beat per clock for every even source/destination head.
    // For a negative first source index the virtual preceding beat is zero;
    // its lanes are masked by the existing destination head strobe.
    wire copy_negative_head = source_head_q<destination_head_q;
    wire [15:0] copy_next_index = out_beat_count_q+(copy_negative_head ? 16'd0 : 16'd1);
    wire [15:0] copy_read_address = copy_next_index<source_span_beats ? copy_next_index : source_span_beats-1'b1;
    wire [4:0] copy_shift_bytes = {1'b0,source_head_q}-{1'b0,destination_head_q}+
        (copy_negative_head ? 5'd16 : 5'd0);
    wire [255:0] copy_window = copy_negative_head && out_beat_count_q==0 ?
        {src_beat_read_q,128'd0} : {src_beat_next_read_q,src_beat_read_q};
    wire [127:0] copy_aligned_data = copy_window >> {copy_shift_bytes,3'b000};
    wire [15:0] vector_strobe =
        (16'hffff << (out_beat_count_q==0 ? destination_head_q : 0)) &
        ((row_pixels_q*2+destination_head_q-out_beat_count_q*16)>=16 ?
         16'hffff : ((16'h0001 << (row_pixels_q*2+destination_head_q-out_beat_count_q*16))-1'b1));
    wire vector_last = (out_beat_count_q+1'b1)*16>=row_pixels_q*2+destination_head_q;
    wire vector_valid, vector_last_out;
    wire [15:0] vector_index_out,vector_strobe_out;
    wire [127:0] vector_data_out;
    wire [3:0] vector_pixels_out;
    // A single shared write address preserves one-port output RAM inference.
    wire [15:0] output_write_address = vector_valid ? vector_index_out : out_beat_count_q;
    usurper_vector_blend vector_blend(.clk(clk_i),.rst_n(rst_n_i),.valid_i(vector_issue),
        .opcode_i(opcode_q),.alpha_i(alpha_q),.key_i(parameter_q[15:0]),
        .index_i(out_beat_count_q),.last_i(vector_last),.source_i(copy_aligned_data),
        .destination_i(dst_beat_read_q),.strobe_i(vector_strobe),.valid_o(vector_valid),
        .index_o(vector_index_out),.last_o(vector_last_out),.data_o(vector_data_out),
        .strobe_o(vector_strobe_out),.pixels_o(vector_pixels_out));
    wire pipe_issue_valid = pipe_issue_q<row_pixels_q;
    wire [15:0] pipe_safe_index = pipe_issue_valid ? pipe_issue_q : 16'd0;
    wire [15:0] pipe_source_linear = source_head_q[3:1]+
        (flags_q[1] ? row_pixels_q-1'b1-pipe_safe_index : pipe_safe_index);
    wire [15:0] source_ram_read_address = tile_pipe_active ? (pipe_source_linear>>3) :
        copy_prefetch ? copy_read_address : src_read_addr_q;
    wire [15:0] source_ram_next_address = source_ram_read_address+1'b1<source_span_beats ?
        source_ram_read_address+1'b1 : source_ram_read_address;
    wire [15:0] pipe_capture_linear = source_head_q[3:1]+
        (flags_q[1] ? row_pixels_q-1'b1-pipe_read_index_q : pipe_read_index_q);
    wire [11:0] pipe_tile_base = (row_y_q-tile_y_q)*TILE_SIZE+(clip_x0_q-tile_x_q);
    wire same_source_beat = (next_src_linear>>3)==(src_linear_d>>3);
    wire same_destination_beat = (next_dst_linear>>3)==(dst_linear_d>>3);

    wire master_req_ready, master_wr_ready, master_rd_valid, master_done;
    wire master_error, master_timeout;
    wire [127:0] master_rd_data;
    wire [127:0] classify_data = state_q==ST_TRI_ROW_RUN ? triangle_data :
        state_q==ST_SRC_CACHE_CHECK ? source_cache_read_q : master_rd_data;
    wire [15:0] master_rd_strb;
    wire [31:0] m_read_bursts, m_write_bursts, m_read_beats, m_write_beats;
    wire [31:0] m_useful_read, m_useful_write, m_bus_read, m_bus_write;
    wire [31:0] m_read_stalls, m_write_stalls, m_partial, m_full;
    wire request_read = state_q==ST_TRI_DESC_REQ || state_q==ST_SRC_REQ || state_q==ST_DST_REQ ||
                        state_q==ST_SRC_CACHE_MISS_REQ ||
                        state_q==ST_TILE_LOAD_REQ;
    wire request_write = state_q==ST_WRITE_REQ || state_q==ST_TILE_FLUSH_WRITE_REQ;
    wire [31:0] request_addr = state_q==ST_TRI_DESC_REQ ? src_base_q :
                               state_q==ST_SRC_REQ ? source_row_addr_q :
                               state_q==ST_SRC_CACHE_MISS_REQ ? source_cache_fill_addr_q :
                               state_q==ST_DST_REQ ? destination_row_addr_q :
                               state_q==ST_TILE_LOAD_REQ ? destination_row_addr_q :
                               destination_row_addr_q;
    wire [23:0] request_bytes = state_q==ST_TRI_DESC_REQ ? 24'd64 :
                                state_q==ST_SRC_CACHE_MISS_REQ ? CACHE_BLOCK_BYTES :
                                state_q==ST_TILE_LOAD_REQ ||
                                state_q==ST_TILE_FLUSH_WRITE_REQ ? tile_width_q*2 :
                                row_pixels_q*2;
    wire write_stream_state = state_q==ST_WRITE_STREAM || state_q==ST_TILE_FLUSH_WRITE;
    wire advance_output_read = STREAM_TRANSFERS && write_stream_state &&
        write_data_valid_q && master_wr_ready && write_beat_index_q+1'b1<out_beat_count_q;
    wire [15:0] output_read_address = advance_output_read ? write_beat_index_q+1'b1 : out_read_addr_q;
    wire flush_prefetch = STREAM_TRANSFERS && state_q==ST_TILE_FLUSH_PACK;
    wire [11:0] flush_read_address = tile_row_q*TILE_SIZE+
        (process_index_q+1'b1<tile_width_q ? process_index_q+1'b1 : process_index_q);

    axi128_master_pb3 #(
        .MAX_BURST_BEATS(MAX_BURST_BEATS), .OUTSTANDING_READS(OUTSTANDING_READS),
        .OUTSTANDING_WRITES(OUTSTANDING_WRITES)
    ) master (
        .clk_i(clk_i), .rst_n_i(rst_n_i), .req_valid_i(request_read || request_write),
        .req_ready_o(master_req_ready), .req_write_i(request_write),
        .req_addr_i(request_addr), .req_bytes_i(request_bytes),
        .wr_valid_i(write_stream_state && write_data_valid_q), .wr_ready_o(master_wr_ready),
        .wr_data_i(out_data_read_q), .wr_strb_i(out_strb_read_q),
        .rd_valid_o(master_rd_valid), .rd_ready_i(1'b1),
        .rd_data_o(master_rd_data), .rd_strb_o(master_rd_strb),
        .done_o(master_done), .error_o(master_error), .timeout_o(master_timeout),
        .axi_read_bursts_o(m_read_bursts), .axi_write_bursts_o(m_write_bursts),
        .axi_read_beats_o(m_read_beats), .axi_write_beats_o(m_write_beats),
        .axi_useful_read_bytes_o(m_useful_read), .axi_useful_write_bytes_o(m_useful_write),
        .axi_bus_read_bytes_o(m_bus_read), .axi_bus_write_bytes_o(m_bus_write),
        .read_stall_cycles_o(m_read_stalls), .write_stall_cycles_o(m_write_stalls),
        .partial_write_beats_o(m_partial), .full_write_beats_o(m_full),
        .max_read_outstanding_o(max_read_outstanding_o),
        .m_axi_awaddr(m_axi_awaddr), .m_axi_awlen(m_axi_awlen),
        .m_axi_awsize(m_axi_awsize), .m_axi_awburst(m_axi_awburst),
        .m_axi_awvalid(m_axi_awvalid), .m_axi_awready(m_axi_awready),
        .m_axi_wdata(m_axi_wdata), .m_axi_wstrb(m_axi_wstrb),
        .m_axi_wlast(m_axi_wlast), .m_axi_wvalid(m_axi_wvalid),
        .m_axi_wready(m_axi_wready), .m_axi_bresp(m_axi_bresp),
        .m_axi_bvalid(m_axi_bvalid), .m_axi_bready(m_axi_bready),
        .m_axi_araddr(m_axi_araddr), .m_axi_arlen(m_axi_arlen),
        .m_axi_arsize(m_axi_arsize), .m_axi_arburst(m_axi_arburst),
        .m_axi_arvalid(m_axi_arvalid), .m_axi_arready(m_axi_arready),
        .m_axi_rdata(m_axi_rdata), .m_axi_rresp(m_axi_rresp),
        .m_axi_rlast(m_axi_rlast), .m_axi_rvalid(m_axi_rvalid),
        .m_axi_rready(m_axi_rready)
    );

    assign debug_signature_o = command_read_q[31:0] ^ command_read_q[95:64] ^
        command_read_q[159:128] ^ command_read_q[223:192] ^
        {16'b0,tile_read_q} ^ m_read_bursts ^ m_write_bursts;

    // Examine only pixels inside the clipped row, excluding AXI head/tail padding.
    // Mixed zero/full-alpha rows need no background read when zero-alpha writes
    // are masked. Fractional alpha always retains the original blending path.
    always @* begin
        beat_has_write_d=0; beat_needs_destination_d=0;
        classify_pixel_d=0; classify_offset=0;
        for(classify_lane=0;classify_lane<8;classify_lane=classify_lane+1) begin
            classify_offset=(read_beat_index_q*8)+classify_lane;
            classify_pixel_d=classify_data[classify_lane*16 +: 16];
            if(classify_offset>=source_head_q[3:1] &&
               classify_offset<source_head_q[3:1]+row_pixels_q) begin
                case(opcode_q)
                    OP_KEY: if(classify_pixel_d!=parameter_q[15:0]) beat_has_write_d=1;
                    OP_ALPHA: if(alpha_q!=0) begin
                        beat_has_write_d=1;
                        if(alpha_q!=255)beat_needs_destination_d=1;
                    end
                    OP_PIXEL: if(alpha_q!=0 && classify_pixel_d[15:12]!=0) begin
                        beat_has_write_d=1;
                        if(alpha_q!=255 || classify_pixel_d[15:12]!=15)
                            beat_needs_destination_d=1;
                    end
                    OP_MASK: if(alpha_q!=0 && classify_pixel_d[7:0]!=0) begin
                        beat_has_write_d=1;
                        if(alpha_q!=255 || classify_pixel_d[7:0]!=255)
                            beat_needs_destination_d=1;
                    end
                    default: begin beat_has_write_d=1; beat_needs_destination_d=0; end
                endcase
            end
        end
    end

    always @* begin
        dx0_d = {{2{cmd_dst_x[15]}},cmd_dst_x};
        dy0_d = {{2{cmd_dst_y[15]}},cmd_dst_y};
        dx1_d = dx0_d + $signed({2'b00,cmd_width});
        dy1_d = dy0_d + $signed({2'b00,cmd_height});
        bound_x0_d = mode_tile_q ? $signed({2'b00,tile_x_q}) : 0;
        bound_y0_d = mode_tile_q ? $signed({2'b00,tile_y_q}) : 0;
        bound_x1_d = mode_tile_q ? $signed({2'b00,tile_x_q+tile_width_q}) : $signed({2'b00,frame_width_q});
        bound_y1_d = mode_tile_q ? $signed({2'b00,tile_y_q+tile_height_q}) : $signed({2'b00,frame_height_q});
        ix0_d = dx0_d > bound_x0_d ? dx0_d : bound_x0_d;
        iy0_d = dy0_d > bound_y0_d ? dy0_d : bound_y0_d;
        ix1_d = dx1_d < bound_x1_d ? dx1_d : bound_x1_d;
        iy1_d = dy1_d < bound_y1_d ? dy1_d : bound_y1_d;

        local_y_d = $signed({2'b00,row_y_q}) - $signed(dst_y_q);
        source_y_d = $signed(src_y_q) +
            (flags_q[2] ? $signed({2'b00,height_q-1'b1}) - local_y_d : local_y_d);
        if (flags_q[1])
            source_min_x_d = $signed(src_x_q) + $signed({2'b00,width_q}) -
                             ($signed({2'b00,clip_x1_q}) - $signed(dst_x_q));
        else
            source_min_x_d = $signed(src_x_q) +
                             ($signed({2'b00,clip_x0_q}) - $signed(dst_x_q));
        source_row_addr_d = src_base_q + source_y_d * src_stride_q + source_min_x_d * 2;
        destination_row_addr_d = dst_base_q + row_y_q * dst_stride_q + clip_x0_q * 2;

        src_linear_d = source_head_q[3:1] +
            (flags_q[1] ? row_pixels_q-1'b1-process_index_q : process_index_q);
        dst_linear_d = destination_head_q[3:1] + process_index_q;
        source_pixel_d = beat_pixel(src_beat_read_q,src_linear_d[2:0]);
        destination_pixel_d = mode_tile_q ? tile_read_q :
                              beat_pixel(dst_beat_read_q,dst_linear_d[2:0]);
        render_pixel_d = destination_pixel_q;
        render_write_d = 1'b0;
        case (opcode_q)
            OP_FILL,OP_CLEAR: begin render_pixel_d=parameter_q[15:0]; render_write_d=1'b1; end
            OP_OPAQUE: begin render_pixel_d=source_pixel_q; render_write_d=1'b1; end
            OP_KEY: begin render_pixel_d=source_pixel_q; render_write_d=source_pixel_q!=parameter_q[15:0]; end
            OP_ALPHA: begin render_pixel_d=alpha_pixel(source_pixel_q,destination_pixel_q,alpha_q); render_write_d=1'b1; end
            OP_PIXEL,OP_MASK: begin
                render_pixel_d=alpha_argb(source_pixel_q,destination_pixel_q,alpha_q,
                                         scalar_mask_format_q,parameter_q[15:0]);
                render_write_d=!OPTIMIZE_SOURCE_ROWS || (alpha_q!=0 &&
                    (scalar_mask_format_q ? source_pixel_q[7:0]!=0 : source_pixel_q[15:12]!=0));
            end
            default: begin render_pixel_d=destination_pixel_q; render_write_d=1'b0; end
        endcase
        pack_lane_d = (destination_head_q[3:1] + process_index_q) & 7;
        pack_data_d = pack_data_q;
        pack_strb_d = pack_strb_q;
        pack_data_d[pack_lane_d*16 +: 16] = render_pixel_d;
        pack_strb_d[pack_lane_d*2 +: 2] = render_write_d ? 2'b11 : 2'b00;
        process_tile_addr_d = (row_y_q-tile_y_q)*TILE_SIZE +
                              (clip_x0_q-tile_x_q) + process_index_q;
    end

    // Build conservative bounds once per submission. END and malformed commands
    // force a group through the normal decoder, preserving termination/errors.
    always @* begin
        group_accum_d=group_accum_q;
        if(cmd_opcode!=OP_NOP) begin
            if(cmd_opcode==OP_END || command_invalid)group_accum_d[73]=1;
            else begin
                group_accum_d[72]=1;
                if(!group_accum_q[72] || dx0_d<$signed(group_accum_q[71:54]))group_accum_d[71:54]=dx0_d;
                if(!group_accum_q[72] || dy0_d<$signed(group_accum_q[53:36]))group_accum_d[53:36]=dy0_d;
                if(!group_accum_q[72] || dx1_d>$signed(group_accum_q[35:18]))group_accum_d[35:18]=dx1_d;
                if(!group_accum_q[72] || dy1_d>$signed(group_accum_q[17:0]))group_accum_d[17:0]=dy1_d;
            end
        end
    end

    always @(posedge clk_i) begin
        if (cmd_we_i && !busy_o)
            command_ram[cmd_addr_i] <= cmd_wdata_i;
        command_read_q <= command_ram[busy_o ? command_index_q[COMMAND_ADDR_WIDTH-1:0] : cmd_addr_i];
        group_read_q <= group_ram[command_index_q[COMMAND_ADDR_WIDTH-1:0]>>GROUP_SHIFT];
        tile_read_q <= tile_ram[tile_pipe_active ? pipe_tile_base+pipe_safe_index[11:0] :
                               flush_prefetch ? flush_read_address : tile_addr_q];
        src_beat_read_q <= src_beat_ram[source_ram_read_address];
        src_beat_next_read_q <= src_beat_ram[source_ram_next_address];
        dst_beat_read_q <= dst_beat_ram[vector_issue && !vector_last ? out_beat_count_q+1'b1 : dst_read_addr_q];
        out_data_read_q <= out_data_ram[output_read_address];
        out_strb_read_q <= out_strb_ram[output_read_address];
        source_cache_read_q <= source_cache_data[source_cache_read_addr_q];
        source_cache_tag_read_q <= source_cache_tag[source_cache_read_addr_q[8:4]];
        source_cache_valid_read_q <= source_cache_valid[source_cache_read_addr_q[8:4]];
        if (tile_we_q)
            tile_ram[tile_write_addr_q] <= tile_wdata_q;
    end

    always @(posedge clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin
            state_q<=ST_IDLE; busy_o<=0; done_o<=0; error_o<=0; mode_tile_q<=0;
            frame_width_q<=0; frame_height_q<=0; framebuffer_stride_q<=0; framebuffer_base_q<=0;
            command_count_q<=0; command_index_q<=0; command_q<=0;
            opcode_q<=0; scalar_mask_format_q<=0;triangle_q<=0;triangle_loaded_q<=0;triangle_descriptor_q<=0; flags_q<=0; alpha_q<=0; src_base_q<=0; dst_base_q<=0; parameter_q<=0;
            src_x_q<=0; src_y_q<=0; dst_x_q<=0; dst_y_q<=0; width_q<=0; height_q<=0;
            src_stride_q<=0; dst_stride_q<=0; clip_x0_q<=0; clip_y0_q<=0; clip_x1_q<=0; clip_y1_q<=0;
            row_y_q<=0; row_pixels_q<=0; process_index_q<=0;
            tile_x_q<=0; tile_y_q<=0; tile_width_q<=0; tile_height_q<=0; tile_row_q<=0;
            source_row_addr_q<=0; destination_row_addr_q<=0; source_head_q<=0; destination_head_q<=0;
            read_beat_index_q<=0; write_beat_index_q<=0; out_beat_count_q<=0;
            pack_data_q<=0; pack_strb_q<=0; tile_addr_q<=0; tile_we_q<=0; tile_wdata_q<=0;
            src_read_addr_q<=0;dst_read_addr_q<=0;out_read_addr_q<=0;
            source_pixel_q<=0;destination_pixel_q<=0;
            source_cache_read_addr_q<=0;
            source_cache_fill_addr_q<=0;source_cache_fill_beat_q<=0;
            source_cache_invalidate_addr_q<=0;
            row_has_write_q<=0;row_needs_destination_q<=0;
            tile_loaded_q<=0;tile_dirty_rows_q<=0;
            tile_write_addr_q<=0;pipe_issue_q<=0;pipe_read_index_q<=0;pipe_apply_index_q<=0;
            pipe_read_valid_q<=0;pipe_apply_valid_q<=0;
            building_groups_q<=0;group_accum_q<=0;
            write_data_valid_q<=0;
            axi_read_bursts_o<=0; axi_write_bursts_o<=0; axi_read_beats_o<=0; axi_write_beats_o<=0;
            axi_useful_read_bytes_o<=0; axi_useful_write_bytes_o<=0;
            axi_bus_read_bytes_o<=0; axi_bus_write_bytes_o<=0;
            read_stall_cycles_o<=0; write_stall_cycles_o<=0;
            partial_write_beats_o<=0; full_write_beats_o<=0; perf_pixels_o<=0;
            source_cache_hits_o<=0;source_cache_misses_o<=0;source_cache_refill_bytes_o<=0;
            base_read_bursts_q<=0; base_write_bursts_q<=0; base_read_beats_q<=0; base_write_beats_q<=0;
            base_useful_read_q<=0; base_useful_write_q<=0; base_bus_read_q<=0; base_bus_write_q<=0;
            base_read_stall_q<=0; base_write_stall_q<=0; base_partial_q<=0; base_full_q<=0;
            for(source_cache_index=0;source_cache_index<CACHE_BLOCKS;
                source_cache_index=source_cache_index+1)
                source_cache_valid[source_cache_index]<=0;
        end else begin
            done_o<=0; tile_we_q<=0;
            if (master_error || master_timeout) error_o<=1;
            if(vector_valid) begin
                out_data_ram[output_write_address]<=vector_data_out;
                out_strb_ram[output_write_address]<=vector_strobe_out;
                perf_pixels_o<=perf_pixels_o+vector_pixels_out;
                if(vector_last_out) begin
                    write_beat_index_q<=0;out_read_addr_q<=0;write_data_valid_q<=0;
                    state_q<=ST_WRITE_REQ;
                end
            end
            case (state_q)
                ST_IDLE: if (start_i) begin
                    busy_o<=1; error_o<=frame_width_i==0 || frame_height_i==0 ||
                        command_count_i==0 || frame_width_i>MAX_ROW_PIXELS;
                    mode_tile_q<=tile_mode_i; frame_width_q<=frame_width_i;
                    frame_height_q<=frame_height_i; framebuffer_base_q<=framebuffer_base_i;
                    framebuffer_stride_q<=framebuffer_stride_i; command_count_q<=command_count_i;
                    command_index_q<=0; perf_pixels_o<=0; tile_x_q<=0; tile_y_q<=0;
                    source_cache_hits_o<=0;source_cache_misses_o<=0;source_cache_refill_bytes_o<=0;
                    for(source_cache_index=0;source_cache_index<CACHE_BLOCKS;
                        source_cache_index=source_cache_index+1)
                        source_cache_valid[source_cache_index]<=0;
                    base_read_bursts_q<=m_read_bursts; base_write_bursts_q<=m_write_bursts;
                    base_read_beats_q<=m_read_beats; base_write_beats_q<=m_write_beats;
                    base_useful_read_q<=m_useful_read; base_useful_write_q<=m_useful_write;
                    base_bus_read_q<=m_bus_read; base_bus_write_q<=m_bus_write;
                    base_read_stall_q<=m_read_stalls; base_write_stall_q<=m_write_stalls;
                    base_partial_q<=m_partial; base_full_q<=m_full;
                    building_groups_q<=tile_mode_i && FILTER_TILE_GROUPS;group_accum_q<=0;
                    state_q<=(tile_mode_i && !FILTER_TILE_GROUPS) ? ST_TILE_SETUP : ST_CMD_ADDR;
                end
                ST_CMD_ADDR: state_q<=ST_CMD_FETCH;
                ST_CMD_FETCH: begin command_q<=command_read_q; state_q<=ST_DECODE; end
                ST_DECODE: begin
                    opcode_q<=cmd_opcode==OP_TRIANGLE ? OP_MASK : cmd_opcode;
                    triangle_q<=cmd_opcode==OP_TRIANGLE;triangle_loaded_q<=0;
                    scalar_mask_format_q<=cmd_opcode==OP_MASK || cmd_opcode==OP_TRIANGLE; flags_q<=cmd_flags; alpha_q<=cmd_alpha;
                    src_base_q<=cmd_src_base; dst_base_q<=cmd_dst_base; src_x_q<=cmd_src_x;
                    src_y_q<=cmd_src_y; dst_x_q<=cmd_dst_x; dst_y_q<=cmd_dst_y;
                    width_q<=cmd_width; height_q<=cmd_height; src_stride_q<=cmd_src_stride;
                    dst_stride_q<=cmd_dst_stride; parameter_q<=cmd_parameter;
                    if (command_index_q>=command_count_q) begin error_o<=1; state_q<=ST_COMPLETE; end
                    else if(building_groups_q) begin
                        group_accum_q<=group_accum_d;
                        if(group_end || cmd_opcode==OP_END || command_index_q+1'b1==command_count_q) begin
                            group_ram[command_index_q[COMMAND_ADDR_WIDTH-1:0]>>GROUP_SHIFT]<=group_accum_d;
                            group_accum_q<=0;
                        end
                        if(cmd_opcode==OP_END || command_index_q+1'b1==command_count_q) begin
                            building_groups_q<=0;state_q<=ST_TILE_SETUP;
                        end else begin command_index_q<=command_index_q+1'b1;state_q<=ST_CMD_ADDR;end
                    end
                    else if (cmd_opcode==OP_END) begin
                        if (mode_tile_q) begin
                            tile_row_q<=0;
                            state_q<=(OPTIMIZE_TILE_IO && tile_dirty_rows_q==0) ? ST_TILE_NEXT : ST_TILE_FLUSH_INIT;
                        end
                        else state_q<=ST_COMPLETE;
                    end else if (cmd_opcode==OP_NOP) state_q<=ST_NEXT_COMMAND;
                    else if (command_invalid) begin
                        error_o<=1; state_q<=ST_COMPLETE;
                    end else if (ix0_d>=ix1_d || iy0_d>=iy1_d) state_q<=ST_NEXT_COMMAND;
                    else begin
                        clip_x0_q<=ix0_d[15:0]; clip_y0_q<=iy0_d[15:0];
                        clip_x1_q<=ix1_d[15:0]; clip_y1_q<=iy1_d[15:0];
                        row_y_q<=iy0_d[15:0]; row_pixels_q<=ix1_d-ix0_d;
                        state_q<=ST_ROW_SETUP;
                        if(OPTIMIZE_TILE_IO && mode_tile_q && !tile_loaded_q) begin
                            // Only the first intersecting command can discard the
                            // old tile, and only when its Fill covers every pixel.
                            if((cmd_opcode==OP_FILL || cmd_opcode==OP_CLEAR) &&
                               ix0_d==bound_x0_d && iy0_d==bound_y0_d &&
                               ix1_d==bound_x1_d && iy1_d==bound_y1_d)
                                tile_loaded_q<=1;
                            else begin
                                tile_row_q<=0;
                                destination_row_addr_q<=framebuffer_base_q+tile_y_q*framebuffer_stride_q+tile_x_q*2;
                                state_q<=ST_TILE_LOAD_REQ;
                            end
                        end
                    end
                end
                ST_ROW_SETUP: begin
                    row_has_write_q<=0;row_needs_destination_q<=0;
                    source_row_addr_q<=source_row_addr_d;
                    destination_row_addr_q<=destination_row_addr_d;
                    source_head_q<=triangle_q ? 4'd0 : source_row_addr_d[3:0];
                    destination_head_q<=destination_row_addr_d[3:0];
                    read_beat_index_q<=0;
                    if (row_pixels_q>MAX_ROW_PIXELS ||
                        ((opcode_q!=OP_FILL && opcode_q!=OP_CLEAR) &&
                         (source_y_d<0 || source_min_x_d<0))) begin error_o<=1; state_q<=ST_COMPLETE; end
                    else if(triangle_q)state_q<=triangle_loaded_q ? ST_TRI_ROW_START : ST_TRI_DESC_REQ;
                    else if (opcode_q==OP_FILL || opcode_q==OP_CLEAR) state_q<=ST_PROCESS_INIT;
                    // Wide, one-use opaque rows avoid the 256-byte source cache's
                    // three-state hit path and serialized miss/refill transactions.
                    // Reuse the existing whole-row DMA reader; it completes before
                    // any destination write, preserving immediate row alias behavior.
                    // Small sprites, flipped-X and all Tile rows keep prior routing.
                    else if(row_pixels_q<=NARROW_DIRECT_PIXELS ||
                            (fast_copy_active && row_pixels_q>=128))state_q<=ST_SRC_REQ;
                    else state_q<=ST_SRC_CACHE_ADDR;
                end
                ST_TRI_DESC_REQ: if(master_req_ready)begin
                    read_beat_index_q<=0;state_q<=ST_TRI_DESC_READ;
                end
                ST_TRI_DESC_READ: begin
                    if(master_rd_valid)begin
                        triangle_descriptor_q[read_beat_index_q[1:0]*128+:128]<=master_rd_data;
                        read_beat_index_q<=read_beat_index_q+1'b1;
                    end
                    if(master_done)begin triangle_loaded_q<=1;state_q<=ST_ROW_SETUP;end
                end
                ST_TRI_ROW_START: if(triangle_ready)state_q<=ST_TRI_ROW_RUN;
                ST_TRI_ROW_RUN: begin
                    if(triangle_valid)begin
                        src_beat_ram[read_beat_index_q]<=triangle_data;
                        read_beat_index_q<=read_beat_index_q+1'b1;
                        row_has_write_q<=row_has_write_q|beat_has_write_d;
                        row_needs_destination_q<=row_needs_destination_q|beat_needs_destination_d;
                    end
                    if(triangle_done)begin
                        read_beat_index_q<=0;
                        if(triangle_error)begin error_o<=1;state_q<=ST_COMPLETE;end
                        else if(classify_source)state_q<=ST_SOURCE_CLASSIFY;
                        else if(!mode_tile_q)state_q<=ST_DST_REQ;
                        else state_q<=ST_PROCESS_INIT;
                    end
                end
                ST_SRC_CACHE_ADDR: begin
                    source_cache_read_addr_q<=source_cache_beat_addr[12:4];
                    state_q<=ST_SRC_CACHE_WAIT;
                end
                ST_SRC_CACHE_WAIT: state_q<=ST_SRC_CACHE_CHECK;
                ST_SRC_CACHE_CHECK: begin
                    if(source_cache_valid_read_q &&
                       source_cache_tag_read_q==source_cache_beat_addr[31:13]) begin
                        row_has_write_q<=row_has_write_q|beat_has_write_d;
                        row_needs_destination_q<=row_needs_destination_q|beat_needs_destination_d;
                        src_beat_ram[read_beat_index_q]<=source_cache_read_q;
                        source_cache_hits_o<=source_cache_hits_o+1'b1;
                        if(read_beat_index_q+1'b1==source_span_beats) begin
                            read_beat_index_q<=0;
                            if(classify_source) state_q<=ST_SOURCE_CLASSIFY;
                            else if(!mode_tile_q && is_alpha) state_q<=ST_DST_REQ;
                            else state_q<=ST_PROCESS_INIT;
                        end else begin
                            read_beat_index_q<=read_beat_index_q+1'b1;
                            state_q<=ST_SRC_CACHE_ADDR;
                        end
                    end else begin
                        source_cache_misses_o<=source_cache_misses_o+1'b1;
                        source_cache_fill_addr_q<={source_cache_beat_addr[31:8],8'b0};
                        state_q<=ST_SRC_CACHE_MISS_REQ;
                    end
                end
                ST_SRC_CACHE_MISS_REQ: if(master_req_ready) begin
                    source_cache_fill_beat_q<=0;
                    state_q<=ST_SRC_CACHE_MISS_READ;
                end
                ST_SRC_CACHE_MISS_READ: begin
                    if(master_rd_valid) begin
                        source_cache_data[{source_cache_fill_addr_q[12:8],
                                           source_cache_fill_beat_q[3:0]}]<=master_rd_data;
                        source_cache_fill_beat_q<=source_cache_fill_beat_q+1'b1;
                    end
                    if(master_done) begin
                        source_cache_tag[source_cache_fill_addr_q[12:8]]<=
                            source_cache_fill_addr_q[31:13];
                        source_cache_valid[source_cache_fill_addr_q[12:8]]<=1;
                        source_cache_refill_bytes_o<=source_cache_refill_bytes_o+CACHE_BLOCK_BYTES;
                        state_q<=ST_SRC_CACHE_ADDR;
                    end
                end
                ST_SRC_REQ: if (master_req_ready) begin read_beat_index_q<=0; state_q<=ST_SRC_READ; end
                ST_SRC_READ: begin
                    if (master_rd_valid) begin
                        row_has_write_q<=row_has_write_q|beat_has_write_d;
                        row_needs_destination_q<=row_needs_destination_q|beat_needs_destination_d;
                        src_beat_ram[read_beat_index_q]<=master_rd_data;
                        read_beat_index_q<=read_beat_index_q+1'b1;
                    end
                    if (master_done) begin read_beat_index_q<=0;
                        if(classify_source) state_q<=ST_SOURCE_CLASSIFY;
                        else if (!mode_tile_q && is_alpha) state_q<=ST_DST_REQ;
                        else state_q<=ST_PROCESS_INIT;
                    end
                end
                ST_SOURCE_CLASSIFY: begin
                    if(!row_has_write_q)state_q<=ST_NEXT_ROW;
                    else if(!mode_tile_q && row_needs_destination_q)state_q<=ST_DST_REQ;
                    else state_q<=ST_PROCESS_INIT;
                end
                ST_DST_REQ: if (master_req_ready) begin read_beat_index_q<=0; state_q<=ST_DST_READ; end
                ST_DST_READ: begin
                    if (master_rd_valid) begin dst_beat_ram[read_beat_index_q]<=master_rd_data; read_beat_index_q<=read_beat_index_q+1'b1; end
                    if (master_done) state_q<=ST_PROCESS_INIT;
                end
                ST_PROCESS_INIT: begin
                    process_index_q<=0; out_beat_count_q<=0; pack_data_q<=0; pack_strb_q<=0;
                    src_read_addr_q<=(source_head_q[3:1]+(flags_q[1]?row_pixels_q-1'b1:0))>>3;
                    dst_read_addr_q<=destination_head_q[3:1]>>3;
                    if (mode_tile_q) begin
                        tile_addr_q<=(row_y_q-tile_y_q)*TILE_SIZE+(clip_x0_q-tile_x_q);
                        if(PIPELINED_TILE_PIXELS) begin
                            pipe_issue_q<=0;pipe_read_valid_q<=0;pipe_apply_valid_q<=0;
                            state_q<=ST_TILE_PIPE;
                        end else state_q<=ST_TILE_PIXEL_WAIT;
                    end
                    else state_q<=ST_PROCESS_WAIT;
                end
                ST_PROCESS_WAIT: state_q<=ST_PROCESS_CAPTURE;
                ST_PROCESS_CAPTURE: begin
                    source_pixel_q<=source_pixel_d;
                    destination_pixel_q<=destination_pixel_d;
                    state_q<=mode_tile_q?ST_TILE_PIXEL_APPLY:ST_PROCESS_IMM;
                end
                ST_PROCESS_IMM: if(vector_active) begin
                    out_beat_count_q<=out_beat_count_q+1'b1;
                    if(vector_last)state_q<=ST_VECTOR_DRAIN;
                end else if(opcode_q==OP_FILL || opcode_q==OP_CLEAR || fast_copy_active) begin
                    // Fill is source-independent: produce one full AXI beat per
                    // cycle, preserving head/tail strobes for clipped rectangles.
                    out_data_ram[output_write_address]<=fast_copy_active ? copy_aligned_data : {8{parameter_q[15:0]}};
                    out_strb_ram[output_write_address]<=
                        (16'hffff << (out_beat_count_q==0 ? destination_head_q : 0)) &
                        ((row_pixels_q*2+destination_head_q-out_beat_count_q*16)>=16 ?
                         16'hffff : ((16'h0001 << (row_pixels_q*2+destination_head_q-out_beat_count_q*16))-1'b1));
                    out_beat_count_q<=out_beat_count_q+1'b1;
                    if((out_beat_count_q+1'b1)*16>=row_pixels_q*2+destination_head_q) begin
                        perf_pixels_o<=perf_pixels_o+row_pixels_q;
                        write_beat_index_q<=0;out_read_addr_q<=0;write_data_valid_q<=0;state_q<=ST_WRITE_REQ;
                    end
                end else begin
                    pack_data_q<=pack_data_d; pack_strb_q<=pack_strb_d;
                    if (pack_lane_d==7 || process_index_q+1'b1==row_pixels_q) begin
                        out_data_ram[output_write_address]<=pack_data_d;
                        out_strb_ram[output_write_address]<=pack_strb_d;
                        out_beat_count_q<=out_beat_count_q+1'b1; pack_data_q<=0; pack_strb_q<=0;
                    end
                    if (render_write_d) perf_pixels_o<=perf_pixels_o+1'b1;
                    if (process_index_q+1'b1==row_pixels_q) begin
                        write_beat_index_q<=0;out_read_addr_q<=0;write_data_valid_q<=0;state_q<=ST_WRITE_REQ;
                    end else begin
                        process_index_q<=process_index_q+1'b1;
                        src_read_addr_q<=(source_head_q[3:1]+
                            (flags_q[1]?row_pixels_q-2'b10-process_index_q:process_index_q+1'b1))>>3;
                        dst_read_addr_q<=(destination_head_q[3:1]+process_index_q+1'b1)>>3;
                        if(same_source_beat && (!is_alpha || same_destination_beat))begin
                            source_pixel_q<=beat_pixel(src_beat_read_q,next_src_linear[2:0]);
                            destination_pixel_q<=beat_pixel(dst_beat_read_q,next_dst_linear[2:0]);
                            state_q<=ST_PROCESS_IMM;
                        end else state_q<=ST_PROCESS_WAIT;
                    end
                end
                ST_VECTOR_DRAIN: begin end // The last pipeline commit starts the AXI write.
                ST_TILE_PIXEL_ADDR: begin
                    tile_addr_q<=process_tile_addr_d;
                    src_read_addr_q<=src_linear_d>>3;
                    state_q<=ST_TILE_PIXEL_WAIT;
                end
                ST_TILE_PIXEL_WAIT: state_q<=ST_PROCESS_CAPTURE;
                ST_TILE_PIXEL_APPLY: begin
                    if (render_write_d) begin
                        tile_addr_q<=process_tile_addr_d;tile_write_addr_q<=process_tile_addr_d;
                        tile_wdata_q<=render_pixel_d; tile_we_q<=1;
                        perf_pixels_o<=perf_pixels_o+1'b1;
                        tile_dirty_rows_q[row_y_q-tile_y_q]<=1;
                    end
                    if (process_index_q+1'b1==row_pixels_q) state_q<=ST_NEXT_ROW;
                    else begin process_index_q<=process_index_q+1'b1; state_q<=ST_TILE_PIXEL_ADDR; end
                end
                ST_TILE_PIPE: begin
                    // Independent read/write addresses permit one issued pixel
                    // per clock. Drain both stages before advancing the row.
                    pipe_read_valid_q<=pipe_issue_valid;
                    pipe_apply_valid_q<=pipe_read_valid_q;
                    if(pipe_issue_valid) begin
                        pipe_read_index_q<=pipe_issue_q;pipe_issue_q<=pipe_issue_q+1'b1;
                    end
                    if(pipe_read_valid_q) begin
                        source_pixel_q<=beat_pixel(src_beat_read_q,pipe_capture_linear[2:0]);
                        destination_pixel_q<=tile_read_q;
                        pipe_apply_index_q<=pipe_read_index_q;
                    end
                    if(pipe_apply_valid_q) begin
                        if(render_write_d) begin
                            tile_write_addr_q<=pipe_tile_base+pipe_apply_index_q[11:0];
                            tile_wdata_q<=render_pixel_d;tile_we_q<=1;
                            perf_pixels_o<=perf_pixels_o+1'b1;
                            tile_dirty_rows_q[row_y_q-tile_y_q]<=1;
                        end
                        if(pipe_apply_index_q+1'b1==row_pixels_q)state_q<=ST_NEXT_ROW;
                    end
                end
                ST_WRITE_REQ: if (master_req_ready) begin write_beat_index_q<=0;write_data_valid_q<=0;state_q<=ST_WRITE_STREAM; end
                ST_WRITE_STREAM: begin
                    if(!write_data_valid_q)write_data_valid_q<=1;
                    if (master_wr_ready && write_data_valid_q) begin
                        write_beat_index_q<=write_beat_index_q+1'b1;
                        out_read_addr_q<=write_beat_index_q+1'b1;
                        write_data_valid_q<=STREAM_TRANSFERS ? 1'b1 : 1'b0;
                    end
                    if (master_done) begin
                        source_cache_invalidate_addr_q<={destination_row_addr_q[31:8],8'b0};
                        state_q<=ST_CACHE_INVALIDATE_IMM;
                    end
                end
                ST_CACHE_INVALIDATE_IMM: begin
                    if(source_cache_valid[source_cache_invalidate_addr_q[12:8]] &&
                       source_cache_tag[source_cache_invalidate_addr_q[12:8]]==
                           source_cache_invalidate_addr_q[31:13])
                        source_cache_valid[source_cache_invalidate_addr_q[12:8]]<=0;
                    if(source_cache_invalidate_addr_q[31:8]==immediate_write_end_addr[31:8])
                        state_q<=ST_NEXT_ROW;
                    else source_cache_invalidate_addr_q<=source_cache_invalidate_addr_q+CACHE_BLOCK_BYTES;
                end
                ST_NEXT_ROW: begin
                    if (row_y_q+1'b1<clip_y1_q) begin row_y_q<=row_y_q+1'b1; state_q<=ST_ROW_SETUP; end
                    else state_q<=ST_NEXT_COMMAND;
                end
                ST_NEXT_COMMAND: begin
                    command_index_q<=command_index_q+1'b1;
                    state_q<=(FILTER_TILE_GROUPS && mode_tile_q && group_end) ? ST_GROUP_ADDR : ST_CMD_ADDR;
                end
                ST_GROUP_ADDR: state_q<=ST_GROUP_CHECK;
                ST_GROUP_CHECK: begin
                    if(command_index_q>=command_count_q || group_read_q[73] ||
                       (group_read_q[72] && $signed(group_read_q[71:54])<bound_x1_d &&
                        $signed(group_read_q[53:36])<bound_y1_d &&
                        $signed(group_read_q[35:18])>bound_x0_d &&
                        $signed(group_read_q[17:0])>bound_y0_d))state_q<=ST_CMD_ADDR;
                    else begin command_index_q<=next_group_index;state_q<=ST_GROUP_ADDR;end
                end

                ST_TILE_SETUP: begin
                    tile_loaded_q<=0;tile_dirty_rows_q<=0;
                    tile_width_q<=frame_width_q-tile_x_q>TILE_SIZE ? TILE_SIZE : frame_width_q-tile_x_q;
                    tile_height_q<=frame_height_q-tile_y_q>TILE_SIZE ? TILE_SIZE : frame_height_q-tile_y_q;
                    tile_row_q<=0;
                    destination_row_addr_q<=framebuffer_base_q+tile_y_q*framebuffer_stride_q+tile_x_q*2;
                    if(OPTIMIZE_TILE_IO) begin
                        command_index_q<=0;state_q<=FILTER_TILE_GROUPS ? ST_GROUP_ADDR : ST_CMD_ADDR;
                    end
                    else state_q<=ST_TILE_LOAD_REQ;
                end
                ST_TILE_LOAD_REQ: if (master_req_ready) begin read_beat_index_q<=0; state_q<=ST_TILE_LOAD_READ; end
                ST_TILE_LOAD_READ: begin
                    if (master_rd_valid) begin src_beat_ram[read_beat_index_q]<=master_rd_data; read_beat_index_q<=read_beat_index_q+1'b1; end
                    if (master_done) state_q<=ST_TILE_UNPACK_INIT;
                end
                ST_TILE_UNPACK_INIT: begin
                    process_index_q<=0;source_head_q<=destination_row_addr_q[3:0];
                    src_read_addr_q<=0;
                    state_q<=ST_TILE_UNPACK_WAIT;
                end
                ST_TILE_UNPACK_WAIT: state_q<=ST_TILE_UNPACK;
                ST_TILE_UNPACK: begin
                    tile_addr_q<=tile_row_q*TILE_SIZE+process_index_q;
                    tile_write_addr_q<=tile_row_q*TILE_SIZE+process_index_q;
                    tile_wdata_q<=unpack_pixel; tile_we_q<=1;
                    if (process_index_q+1'b1==tile_width_q) begin
                        if (tile_row_q+1'b1==tile_height_q) begin
                            tile_loaded_q<=1;
                            if(OPTIMIZE_TILE_IO)state_q<=ST_ROW_SETUP;
                            else begin
                                command_index_q<=0;state_q<=FILTER_TILE_GROUPS ? ST_GROUP_ADDR : ST_CMD_ADDR;
                            end
                        end
                        else begin
                            tile_row_q<=tile_row_q+1'b1;
                            destination_row_addr_q<=framebuffer_base_q+(tile_y_q+tile_row_q+1'b1)*framebuffer_stride_q+tile_x_q*2;
                            state_q<=ST_TILE_LOAD_REQ;
                        end
                    end else begin
                        process_index_q<=process_index_q+1'b1;
                        src_read_addr_q<=(unpack_linear+1'b1)>>3;
                        state_q<=(STREAM_TRANSFERS && unpack_linear[2:0]!=7) ? ST_TILE_UNPACK : ST_TILE_UNPACK_WAIT;
                    end
                end
                ST_TILE_FLUSH_INIT: begin
                    if(OPTIMIZE_TILE_IO && !tile_dirty_rows_q[tile_row_q]) begin
                        if(tile_row_q+1'b1<tile_height_q)tile_row_q<=tile_row_q+1'b1;
                        else state_q<=ST_TILE_NEXT;
                    end else begin
                    process_index_q<=0; out_beat_count_q<=0; pack_data_q<=0; pack_strb_q<=0;
                    destination_row_addr_q<=framebuffer_base_q+(tile_y_q+tile_row_q)*framebuffer_stride_q+tile_x_q*2;
                    destination_head_q<=(framebuffer_base_q+(tile_y_q+tile_row_q)*framebuffer_stride_q+tile_x_q*2)&15;
                    state_q<=ST_TILE_FLUSH_ADDR;
                    end
                end
                ST_TILE_FLUSH_ADDR: begin tile_addr_q<=tile_row_q*TILE_SIZE+process_index_q; state_q<=ST_TILE_FLUSH_WAIT; end
                ST_TILE_FLUSH_WAIT: state_q<=ST_TILE_FLUSH_PACK;
                ST_TILE_FLUSH_PACK: begin
                    pack_data_q<=pack_data_q; pack_strb_q<=pack_strb_q;
                    pack_data_q[flush_lane*16 +: 16]<=tile_read_q;
                    pack_strb_q[flush_lane*2 +: 2]<=2'b11;
                    if (flush_lane==7 || process_index_q+1'b1==tile_width_q) begin
                        out_data_ram[output_write_address]<=pack_data_q | ({112'b0,tile_read_q} << (flush_lane*16));
                        out_strb_ram[output_write_address]<=pack_strb_q | (16'h0003 << (flush_lane*2));
                        out_beat_count_q<=out_beat_count_q+1'b1; pack_data_q<=0; pack_strb_q<=0;
                    end
                    if (process_index_q+1'b1==tile_width_q) begin
                        write_beat_index_q<=0;out_read_addr_q<=0;write_data_valid_q<=0;state_q<=ST_TILE_FLUSH_WRITE_REQ;
                    end
                    else begin
                        process_index_q<=process_index_q+1'b1;
                        state_q<=STREAM_TRANSFERS ? ST_TILE_FLUSH_PACK : ST_TILE_FLUSH_ADDR;
                    end
                end
                ST_TILE_FLUSH_WRITE_REQ: if (master_req_ready) begin
                    write_beat_index_q<=0;write_data_valid_q<=0;state_q<=ST_TILE_FLUSH_WRITE;
                end
                ST_TILE_FLUSH_WRITE: begin
                    if(!write_data_valid_q)write_data_valid_q<=1;
                    if (master_wr_ready && write_data_valid_q) begin
                        write_beat_index_q<=write_beat_index_q+1'b1;
                        out_read_addr_q<=write_beat_index_q+1'b1;
                        write_data_valid_q<=STREAM_TRANSFERS ? 1'b1 : 1'b0;
                    end
                    if (master_done) begin
                        source_cache_invalidate_addr_q<={destination_row_addr_q[31:8],8'b0};
                        state_q<=ST_CACHE_INVALIDATE_TILE;
                    end
                end
                ST_CACHE_INVALIDATE_TILE: begin
                    if(source_cache_valid[source_cache_invalidate_addr_q[12:8]] &&
                       source_cache_tag[source_cache_invalidate_addr_q[12:8]]==
                           source_cache_invalidate_addr_q[31:13])
                        source_cache_valid[source_cache_invalidate_addr_q[12:8]]<=0;
                    if(source_cache_invalidate_addr_q[31:8]==tile_write_end_addr[31:8]) begin
                        if(tile_row_q+1'b1<tile_height_q) begin
                            tile_row_q<=tile_row_q+1'b1;state_q<=ST_TILE_FLUSH_INIT;
                        end else state_q<=ST_TILE_NEXT;
                    end else source_cache_invalidate_addr_q<=source_cache_invalidate_addr_q+CACHE_BLOCK_BYTES;
                end
                ST_TILE_NEXT: begin
                    if (tile_x_q+TILE_SIZE<frame_width_q) begin tile_x_q<=tile_x_q+TILE_SIZE; state_q<=ST_TILE_SETUP; end
                    else if (tile_y_q+TILE_SIZE<frame_height_q) begin tile_x_q<=0; tile_y_q<=tile_y_q+TILE_SIZE; state_q<=ST_TILE_SETUP; end
                    else state_q<=ST_COMPLETE;
                end
                ST_COMPLETE: begin
                    axi_read_bursts_o<=m_read_bursts-base_read_bursts_q;
                    axi_write_bursts_o<=m_write_bursts-base_write_bursts_q;
                    axi_read_beats_o<=m_read_beats-base_read_beats_q;
                    axi_write_beats_o<=m_write_beats-base_write_beats_q;
                    axi_useful_read_bytes_o<=m_useful_read-base_useful_read_q;
                    axi_useful_write_bytes_o<=m_useful_write-base_useful_write_q;
                    axi_bus_read_bytes_o<=m_bus_read-base_bus_read_q;
                    axi_bus_write_bytes_o<=m_bus_write-base_bus_write_q;
                    read_stall_cycles_o<=m_read_stalls-base_read_stall_q;
                    write_stall_cycles_o<=m_write_stalls-base_write_stall_q;
                    partial_write_beats_o<=m_partial-base_partial_q;
                    full_write_beats_o<=m_full-base_full_q;
                    busy_o<=0; done_o<=1; state_q<=ST_IDLE;
                end
                default: begin error_o<=1; state_q<=ST_COMPLETE; end
            endcase
        end
    end
endmodule

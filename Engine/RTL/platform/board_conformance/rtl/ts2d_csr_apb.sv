`timescale 1ns/1ps
`default_nettype none

// TileStorm board control plane. CPU writes 32-byte commands through APB;
// the bridge assembles them into 256-bit PB3 command words before starting
// the renderer. The module is vendor-neutral and is intended to sit between
// Sapphire APB0 and the PB3 frame_renderer_pb3 AXI master.
module ts2d_csr_apb #(
    parameter integer COMMAND_ADDR_WIDTH = 10
) (
    input  wire clk_i,
    input  wire rst_i,
    input  wire [15:0] apb_paddr_i,
    input  wire apb_psel_i,
    input  wire apb_penable_i,
    input  wire apb_pwrite_i,
    input  wire [31:0] apb_pwdata_i,
    output reg  [31:0] apb_prdata_o,
    output wire apb_pready_o,
    output wire apb_pslverror_o,
    output reg  irq_pending_o,
    input  wire renderer_busy_i,
    input  wire renderer_done_i,
    input  wire renderer_error_i,
    output reg  renderer_cmd_we_o,
    output reg  [COMMAND_ADDR_WIDTH-1:0] renderer_cmd_addr_o,
    output reg  [255:0] renderer_cmd_data_o,
    output reg  renderer_start_o,
    output reg  renderer_tile_mode_o,
    output reg  [15:0] renderer_width_o,
    output reg  [15:0] renderer_height_o,
    output reg  [31:0] renderer_framebuffer_base_o,
    output reg  [15:0] renderer_framebuffer_stride_o,
    output reg  [COMMAND_ADDR_WIDTH:0] renderer_command_count_o
);

    // v2: one 32 KiB command aperture plus a bank selector. Two 32 KiB
    // apertures and CSRs cannot fit into Sapphire's 64 KiB APB window.
    localparam integer CAPACITY = 1 << COMMAND_ADDR_WIDTH;
    reg write_bank, active_bank, inflight, loading, start_pending;
    reg [31:0] control, error_code, complete_count;
    reg [COMMAND_ADDR_WIDTH:0] count, index;
    reg read_pending;
    wire [255:0] read_command;
    reg [63:0] cycles, last_cycles;
    wire access=apb_psel_i && apb_penable_i;
    wire wr=access && apb_pwrite_i;
    wire busy=inflight || loading || start_pending || renderer_busy_i || renderer_start_o;
    wire bad_count=apb_pwdata_i[15:1]==0 || apb_pwdata_i[15:1]>CAPACITY;
    wire aperture=apb_paddr_i>=16'h8000;
    wire conflict=wr && aperture && busy && write_bank==active_bank;
    assign apb_pready_o=1'b1;
    assign apb_pslverror_o=access && (apb_paddr_i[1:0]!=0 || conflict ||
        (wr && apb_paddr_i==16'h1c && (busy || bad_count)) ||
        (wr && (apb_paddr_i==8 || apb_paddr_i==36 || apb_paddr_i==44 || apb_paddr_i==48) && busy));
    always @* begin
        apb_prdata_o=0;
        case(apb_paddr_i)
            0: apb_prdata_o=32'h54533244;
            4: apb_prdata_o=32'h00020000;
            8: apb_prdata_o=control;
            12: apb_prdata_o={26'b0,loading,active_bank,(error_code!=0),1'b0,busy,1'b0};
            16: apb_prdata_o={31'b0,irq_pending_o};
            20: apb_prdata_o=error_code;
            36: apb_prdata_o=renderer_framebuffer_base_o;
            44: apb_prdata_o=renderer_framebuffer_stride_o;
            48: apb_prdata_o={renderer_height_o,renderer_width_o};
            52: apb_prdata_o=write_bank;
            56: apb_prdata_o=CAPACITY;
            60: apb_prdata_o=complete_count;
            84: apb_prdata_o=last_cycles[31:0];
            88: apb_prdata_o=last_cycles[63:32];
            92: apb_prdata_o=renderer_command_count_o;
            default: apb_prdata_o=0;
        endcase
    end
    // Independent word lanes retain arbitrary APB write order and both banks.
    // Parallel reads also let synthesis discard bits unused by the renderer.
    for(genvar word_lane=0;word_lane<8;word_lane=word_lane+1)begin: command_lane
        (* ram_style="block" *) reg [31:0] words[0:CAPACITY*2-1];
        reg [31:0] read_word;
        assign read_command[word_lane*32+:32]=read_word;
        always @(posedge clk_i)begin
            if(wr && aperture && !apb_pslverror_o && apb_paddr_i[4:2]==word_lane)
                words[{write_bank,apb_paddr_i[COMMAND_ADDR_WIDTH+4:5]}]<=apb_pwdata_i;
            if(loading && !read_pending)
                read_word<=words[{active_bank,index[COMMAND_ADDR_WIDTH-1:0]}];
        end
    end
    always @(posedge clk_i) begin
        if(rst_i) begin
            write_bank<=0;active_bank<=0;inflight<=0;loading<=0;start_pending<=0;
            control<=4;error_code<=0;complete_count<=0;count<=0;index<=0;
            read_pending<=0;cycles<=0;last_cycles<=0;irq_pending_o<=0;
            renderer_cmd_we_o<=0;renderer_cmd_addr_o<=0;renderer_cmd_data_o<=0;
            renderer_start_o<=0;renderer_tile_mode_o<=0;
            renderer_width_o<=1024;renderer_height_o<=576;
            renderer_framebuffer_base_o<=32'h01000000;
            renderer_framebuffer_stride_o<=2048;renderer_command_count_o<=0;
        end else begin
            renderer_cmd_we_o<=0;renderer_start_o<=0;
            if(inflight)cycles<=cycles+1'b1;
            if(wr && !apb_pslverror_o)begin
                case(apb_paddr_i)
                    8: control<=apb_pwdata_i;
                    16: if(apb_pwdata_i[0])irq_pending_o<=0;
                    20: error_code<=error_code & ~apb_pwdata_i;
                    36: renderer_framebuffer_base_o<=apb_pwdata_i;
                    44: renderer_framebuffer_stride_o<=apb_pwdata_i[15:0];
                    48: begin renderer_width_o<=apb_pwdata_i[15:0];renderer_height_o<=apb_pwdata_i[31:16];end
                    52: write_bank<=apb_pwdata_i[0];
                    28: begin
                        active_bank<=apb_pwdata_i[0];count<=apb_pwdata_i[15:1];
                        renderer_command_count_o<=apb_pwdata_i[15:1];
                        renderer_tile_mode_o<=control[3:2]==2;
                        index<=0;read_pending<=0;loading<=1;inflight<=1;
                        error_code<=0;irq_pending_o<=0;cycles<=0;
                    end
                endcase
            end
            if(loading)begin
                read_pending<=!read_pending;
                if(read_pending)begin
                    renderer_cmd_we_o<=1;
                    renderer_cmd_addr_o<=index[COMMAND_ADDR_WIDTH-1:0];
                    renderer_cmd_data_o<=read_command;
                    if(index+1>=count)begin loading<=0;start_pending<=1;end
                    else index<=index+1'b1;
                end
            end
            // Start on the cycle AFTER the final command RAM write.
            if(start_pending)begin start_pending<=0;renderer_start_o<=1;end
            if(inflight && renderer_done_i)begin
                inflight<=0;irq_pending_o<=1;last_cycles<=cycles;
                complete_count<=complete_count+1'b1;
                if(renderer_error_i)error_code<=1;
            end
        end
    end
endmodule
`default_nettype wire

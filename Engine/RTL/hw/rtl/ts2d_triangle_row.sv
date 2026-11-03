`timescale 1ns/1ps
// Experimental row producer. A validated 64-byte bounded descriptor supplies
// three half-open edges and an integer coverage numerator. The output is the
// existing uint16 Alpha8 source format (upper byte zero), eight pixels/beat.
// Offsets are relative to the descriptor's clipped bbox. The caller guarantees
// int32 edge/numerator values over the requested rectangle and a matching Q48
// coefficient. No DDR access or changes to the ordinary renderer yet.
module ts2d_triangle_row (
    input wire clk, rst_n, valid_i,
    output wire ready_o,
    input wire [511:0] descriptor_i,
    input wire [2:0] inclusive_i,
    input wire [7:0] x_i, y_i, count_i,
    output wire valid_o,
    input wire ready_i,
    output wire [127:0] data_o,
    output wire [3:0] index_o,
    output wire last_o,
    output reg done_o, error_o
);
    localparam [3:0] IDLE=0,INIT_X=1,INIT_Y=2,CHECK=3,SUM=4,
        ROUND=5,EXACT_REQUEST=6,EXACT_WAIT=7,PACK=8,OUTPUT=9;
    reg [3:0] state_q;
    reg signed [31:0] value_q[0:3],step_q[0:3],down_q[0:3];
    reg [6:0] x_q,y_q;
    reg [2:0] shift_q,inclusive_q;
    reg [7:0] count_q,pixel_q,alpha_q;
    reg [31:0] area_q,paint_q;
    reg [63:0] coefficient_q,rounded_q;
    reg [56:0] product_low_q;
    reg [31:0] product_high_q;
    reg [127:0] pack_q;
    reg [2:0] lane_q;
    reg [3:0] index_q;
    reg last_q;
    assign ready_o=state_q==IDLE;
    assign valid_o=state_q==OUTPUT;
    assign data_o=pack_q;
    assign index_o=index_q;
    assign last_o=last_q;

    wire covered=(value_q[0]>0 || (value_q[0]==0 && inclusive_q[0])) &&
        (value_q[1]>0 || (value_q[1]==0 && inclusive_q[1])) &&
        (value_q[2]>0 || (value_q[2]==0 && inclusive_q[2]));
    wire numerator_valid=value_q[3]>=0 && $unsigned(value_q[3])<=area_q;
    wire safe_round=rounded_q[47:0]>=48'h001000000000 &&
        rounded_q[47:0]<=48'hfff000000000;
    wire exact_ready,exact_valid,exact_error;
    wire [7:0] exact_alpha;
    ts2d_triangle_alpha_exact exact_alpha_unit(
        .clk(clk),.rst_n(rst_n),.valid_i(state_q==EXACT_REQUEST),
        .numerator_i(value_q[3]),.area_i(area_q),.paint_i(paint_q),
        .ready_o(exact_ready),.valid_o(exact_valid),
        .error_o(exact_error),.alpha_o(exact_alpha));

    integer i;
    always @(posedge clk or negedge rst_n)begin
        if(!rst_n)begin
            state_q<=IDLE;done_o<=0;error_o<=0;pack_q<=0;
            index_q<=0;last_q<=0;
        end else begin
            done_o<=0;
            case(state_q)
            IDLE:if(valid_i)begin
                error_o<=0;pack_q<=0;index_q<=0;last_q<=0;
                if(count_i==0 || count_i>128 || x_i>127 || y_i>127 ||
                   {1'b0,x_i}+{1'b0,count_i}>128 ||
                   descriptor_i[479:448]==0 || descriptor_i[479:448]>32'h01000000 ||
                   (descriptor_i[511:480]!=0 && (descriptor_i[511:480]<32'h2f800000 || descriptor_i[511:480]>32'h3f800000)))begin
                    error_o<=1;done_o<=1;
                end else begin
                    for(i=0;i<3;i=i+1)begin
                        value_q[i]<=descriptor_i[i*32+:32];
                        step_q[i]<=descriptor_i[(i+3)*32+:32];
                        down_q[i]<=descriptor_i[(i+6)*32+:32];
                    end
                    value_q[3]<=descriptor_i[319:288];step_q[3]<=descriptor_i[351:320];down_q[3]<=descriptor_i[383:352];
                    coefficient_q<=descriptor_i[447:384];area_q<=descriptor_i[479:448];paint_q<=descriptor_i[511:480];
                    x_q<=x_i[6:0];y_q<=y_i[6:0];count_q<=count_i;pixel_q<=0;lane_q<=0;
                    inclusive_q<=inclusive_i;shift_q<=0;state_q<=INIT_X;
                end
            end
            INIT_X:begin
                if(x_q[shift_q])for(i=0;i<4;i=i+1)value_q[i]<=value_q[i]+(step_q[i]<<<shift_q);
                if(shift_q==6)begin shift_q<=0;state_q<=INIT_Y;end else shift_q<=shift_q+1'b1;
            end
            INIT_Y:begin
                if(y_q[shift_q])for(i=0;i<4;i=i+1)value_q[i]<=value_q[i]+(down_q[i]<<<shift_q);
                if(shift_q==6)state_q<=CHECK;else shift_q<=shift_q+1'b1;
            end
            CHECK:begin
                if(!covered || paint_q==0)begin alpha_q<=0;state_q<=PACK;end
                else if(!numerator_valid)begin error_o<=1;done_o<=1;state_q<=IDLE;end
                else if(value_q[3]==0)begin alpha_q<=0;state_q<=PACK;end
                else begin
                    product_low_q<=$unsigned(value_q[3][24:0])*coefficient_q[31:0];
                    product_high_q<=$unsigned(value_q[3][24:0])*coefficient_q[63:32];
                    state_q<=SUM;
                end
            end
            SUM:begin
                rounded_q<={7'b0,product_low_q}+{product_high_q,32'b0}+64'h0000800000000000;
                state_q<=ROUND;
            end
            ROUND:begin
                if(safe_round)begin alpha_q<=rounded_q[55:48];state_q<=PACK;end
                else state_q<=EXACT_REQUEST;
            end
            EXACT_REQUEST:if(exact_ready)state_q<=EXACT_WAIT;
            EXACT_WAIT:if(exact_valid)begin
                if(exact_error)begin error_o<=1;done_o<=1;state_q<=IDLE;end
                else begin alpha_q<=exact_alpha;state_q<=PACK;end
            end
            PACK:begin
                pack_q[lane_q*16+:16]<={8'b0,alpha_q};
                if(lane_q==7 || pixel_q+1'b1==count_q)begin
                    last_q<=pixel_q+1'b1==count_q;state_q<=OUTPUT;
                end else begin lane_q<=lane_q+1'b1;state_q<=CHECK;end
                if(pixel_q+1'b1!=count_q)begin
                    pixel_q<=pixel_q+1'b1;
                    for(i=0;i<4;i=i+1)value_q[i]<=value_q[i]+step_q[i];
                end
            end
            OUTPUT:if(ready_i)begin
                if(last_q)begin done_o<=1;state_q<=IDLE;end
                else begin index_q<=index_q+1'b1;lane_q<=0;pack_q<=0;state_q<=CHECK;end
            end
            default:begin error_o<=1;done_o<=1;state_q<=IDLE;end
            endcase
        end
    end
endmodule

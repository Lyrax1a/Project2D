`timescale 1ns/1ps
// SPDX-License-Identifier: MIT
// Immediate RGB565/ARGB4444 blend: one aligned eight-pixel beat per clock.
// Three register boundaries separate realignment, opacity, and channel math.
module usurper_vector_blend (
    input wire clk, input wire rst_n, input wire valid_i,
    input wire [7:0] opcode_i, alpha_i,
    input wire [15:0] key_i, index_i,
    input wire last_i,
    input wire [127:0] source_i, destination_i,
    input wire [15:0] strobe_i,
    output wire valid_o, output wire [15:0] index_o,
    output wire last_o, output reg [127:0] data_o,
    output wire [15:0] strobe_o, output reg [3:0] pixels_o
);
    reg [2:0] valid_q, last_q;
    reg [15:0] index_q [0:2];
    reg [15:0] strobe_q [0:2];
    reg [127:0] source_q, destination_q;
    reg pixel_format_q,mask_format_q;
    reg [15:0] color_q;
    reg [16:0] opacity_product_q [0:7];
    reg [7:0] opacity_q [0:7];
    reg [7:0] source_channel_q [0:23], destination_channel_q [0:23];
    reg [16:0] total_q [0:23];
    integer lane, channel, metadata;

    function automatic [7:0] divide255(input [16:0] biased);
        reg [16:0] corrected;
        begin corrected=biased+(biased>>8);divide255=corrected[15:8];end
    endfunction
    function automatic [7:0] expand5(input [4:0] v);
        expand5={v,v[4:2]};
    endfunction
    function automatic [7:0] expand6(input [5:0] v);
        expand6={v,v[5:4]};
    endfunction
    assign valid_o=valid_q[2];
    assign index_o=index_q[2];
    assign last_o=last_q[2];
    assign strobe_o=strobe_q[2];
    always @* begin
        data_o=0;pixels_o=0;
        for(integer output_lane=0;output_lane<8;output_lane=output_lane+1) begin
            // Explicit widths avoid concatenation of shifted eight-bit values.
            data_o[output_lane*16+11 +: 5]=divide255(total_q[output_lane*3]+17'd128)>>3;
            data_o[output_lane*16+5 +: 6]=divide255(total_q[output_lane*3+1]+17'd128)>>2;
            data_o[output_lane*16 +: 5]=divide255(total_q[output_lane*3+2]+17'd128)>>3;
            if(strobe_q[2][output_lane*2]) pixels_o=pixels_o+1'b1;
        end
    end
    always @(posedge clk or negedge rst_n) begin
        if(!rst_n) begin
            valid_q<=0;last_q<=0;
            for(metadata=0;metadata<3;metadata=metadata+1) begin
                index_q[metadata]<=0;strobe_q[metadata]<=0;
            end
        end else begin
            valid_q<={valid_q[1:0],valid_i};
            last_q<={last_q[1:0],last_i};
            index_q[0]<=index_i;
            for(metadata=1;metadata<3;metadata=metadata+1) begin
                index_q[metadata]<=index_q[metadata-1];
                strobe_q[metadata]<=strobe_q[metadata-1];
            end
            if(valid_i) begin
                source_q<=source_i;destination_q<=destination_i;
                pixel_format_q<=opcode_i==8'h05;mask_format_q<=opcode_i==8'h06;color_q<=key_i;
                for(lane=0;lane<8;lane=lane+1) begin
                    strobe_q[0][lane*2 +: 2]<=strobe_i[lane*2 +: 2] &
                        ((opcode_i==8'h03 ? source_i[lane*16 +: 16]!=key_i :
                          alpha_i!=0 && (opcode_i!=8'h05 || source_i[lane*16+12 +: 4]!=0) &&
                          (opcode_i!=8'h06 || source_i[lane*16 +: 8]!=0)) ? 2'b11 : 2'b00);
                    opacity_product_q[lane]<=(opcode_i==8'h05 || opcode_i==8'h06) ?
                        (opcode_i==8'h06 ? source_i[lane*16 +: 8] :
                         {source_i[lane*16+12 +: 4],source_i[lane*16+12 +: 4]})*alpha_i+17'd128 :
                        (opcode_i==8'h03 ? 17'd65153 : ({9'd0,alpha_i}<<8)-alpha_i+17'd128);
                end
            end
            if(valid_q[0]) begin
                for(lane=0;lane<8;lane=lane+1) begin
                    opacity_q[lane]<=divide255(opacity_product_q[lane]);
                    source_channel_q[lane*3]<=mask_format_q ? expand5(color_q[15:11]) : pixel_format_q ?
                        {source_q[lane*16+8 +: 4],source_q[lane*16+8 +: 4]} : expand5(source_q[lane*16+11 +: 5]);
                    source_channel_q[lane*3+1]<=mask_format_q ? expand6(color_q[10:5]) : pixel_format_q ?
                        {source_q[lane*16+4 +: 4],source_q[lane*16+4 +: 4]} : expand6(source_q[lane*16+5 +: 6]);
                    source_channel_q[lane*3+2]<=mask_format_q ? expand5(color_q[4:0]) : pixel_format_q ?
                        {source_q[lane*16 +: 4],source_q[lane*16 +: 4]} : expand5(source_q[lane*16 +: 5]);
                    destination_channel_q[lane*3]<=expand5(destination_q[lane*16+11 +: 5]);
                    destination_channel_q[lane*3+1]<=expand6(destination_q[lane*16+5 +: 6]);
                    destination_channel_q[lane*3+2]<=expand5(destination_q[lane*16 +: 5]);
                end
            end
            if(valid_q[1]) begin
                for(channel=0;channel<24;channel=channel+1) begin
                    // Fully opaque classified rows intentionally skip the DDR
                    // destination read. Never consume uninitialized RAM lanes.
                    if(opacity_q[channel/3]==255)
                        total_q[channel]<=({9'd0,source_channel_q[channel]}<<8)-source_channel_q[channel];
                    else if(opacity_q[channel/3]==0)
                        total_q[channel]<=({9'd0,destination_channel_q[channel]}<<8)-destination_channel_q[channel];
                    else total_q[channel]<=source_channel_q[channel]*opacity_q[channel/3]+
                        destination_channel_q[channel]*(8'd255-opacity_q[channel/3]);
                end
            end
        end
    end
endmodule

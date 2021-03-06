/*
 * Copyright (C) 2015 Jared Boone, ShareBrained Technology, Inc.
 * Copyright (C) 2016 Furrtek
 * 
 * This file is part of PortaPack.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "proc_rds.hpp"
#include "portapack_shared_memory.hpp"
#include "sine_table.hpp"
#include "event_m4.hpp"

#include <cstdint>

void RDSProcessor::execute(const buffer_c8_t& buffer) {
	
	for (size_t i = 0; i < buffer.count; i++) {
		
		//Sample generation 2.28M/10=228kHz
		if(s >= 9) {
			s = 0;
			if(sample_count >= SAMPLES_PER_BIT) {
				cur_bit = (rdsdata[(bit_pos / 26) & 15] >> (25 - (bit_pos % 26))) & 1;
				prev_output = cur_output;
				cur_output = prev_output ^ cur_bit;

				const int32_t *src = waveform_biphase;	// const ok ?
				int idx = in_sample_index;

				for(int j = 0; j < FILTER_SIZE; j++) {
					val = (*src++);
					if (cur_output) val = -val;
					sample_buffer[idx++] += val;
					if (idx >= SAMPLE_BUFFER_SIZE) idx = 0;
				}

				in_sample_index += SAMPLES_PER_BIT;
				if (in_sample_index >= SAMPLE_BUFFER_SIZE) in_sample_index -= SAMPLE_BUFFER_SIZE;
				
				if (bit_pos < shared_memory.bit_length)
					bit_pos++;
				else
					bit_pos = 0;
				sample_count = 0;
			}
			
			sample = sample_buffer[out_sample_index];
			sample_buffer[out_sample_index] = 0;
			out_sample_index++;
			if (out_sample_index >= SAMPLE_BUFFER_SIZE) out_sample_index = 0;
			
			//AM @ 228k/4 = 57kHz
			switch (mphase) {
				case 0:
				case 2: sample = 0; break;
				case 1: break;
				case 3: sample = -sample; break;
			}
			mphase++;
			if (mphase >= 4) mphase = 0;
			
			sample_count++;
		} else {
			s++;
		}
		
		//FM
		frq = (sample>>16) * 386760;
		
		phase = (phase + frq);
		sphase = phase + (256<<16);
		
		re = (sine_table_f32[(sphase & 0x03FF0000)>>18]*127);
		im = (sine_table_f32[(phase & 0x03FF0000)>>18]*127);
		
		buffer.p[i] = {(int8_t)re,(int8_t)im};
	}
}

void RDSProcessor::on_message(const Message* const msg) {
	if (msg->id == Message::ID::RDSConfigure) {
		//const auto message = *reinterpret_cast<const RDSConfigureMessage*>(p);
		rdsdata = (uint32_t*)shared_memory.tx_data;
		configured = true;
	}
}

int main() {
	EventDispatcher event_dispatcher { std::make_unique<RDSProcessor>() };
	event_dispatcher.run();
	return 0;
}

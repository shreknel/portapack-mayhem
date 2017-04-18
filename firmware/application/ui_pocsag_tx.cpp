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

#include "ui_pocsag_tx.hpp"

#include "baseband_api.hpp"
#include "string_format.hpp"
#include "ui_textentry.hpp"

#include "portapack_persistent_memory.hpp"

#include <cstring>
#include <stdio.h>
#include <math.h>

using namespace portapack;
using namespace pocsag;

namespace ui {

void POCSAGTXView::focus() {
	tx_view.focus();
}

POCSAGTXView::~POCSAGTXView() {
	transmitter_model.disable();
	baseband::shutdown();
}

void POCSAGTXView::on_tx_progress(const int progress, const bool done) {
	if (done) {
		transmitter_model.disable();
		progressbar.set_value(0);
		tx_view.set_transmitting(false);
	} else
		progressbar.set_value(progress);
}

bool POCSAGTXView::start_tx() {
	uint32_t total_frames, i, codeword, bi, address;
	pocsag::BitRate bitrate;
	std::vector<uint32_t> codewords;
	MessageType type;
	
	type = (MessageType)options_type.selected_index_value();
	
	address = field_address.value_dec_u32();
	if (address > 0x1FFFFFU) {
		nav_.display_modal("Bad address", "Address must be less\nthan 2097152.", INFO, nullptr);
		return false;
	}
	
	if (type == MessageType::NUMERIC_ONLY) {
		// Check for invalid characters
		if (message.find_first_not_of("0123456789SU -][") != std::string::npos) {
			nav_.display_modal(
				"Bad message",
				"A numeric only message must\nonly contain:\n0123456789SU][- or space.",
				INFO,
				nullptr
			);
			return false;
		}
	}
	
	pocsag_encode(type, BCH_code, message, address, codewords);
	
	total_frames = codewords.size() / 2;
	
	progressbar.set_max(total_frames);
	
	transmitter_model.set_sampling_rate(2280000);
	transmitter_model.set_rf_amp(true);
	transmitter_model.set_lna(40);
	transmitter_model.set_vga(40);
	transmitter_model.set_baseband_bandwidth(1750000);
	transmitter_model.enable();
	
	uint8_t * data_ptr = shared_memory.bb_data.data;
	
	bi = 0;
	for (i = 0; i < codewords.size(); i++) {
		codeword = codewords[i];
		data_ptr[bi++] = (codeword >> 24) & 0xFF;
		data_ptr[bi++] = (codeword >> 16) & 0xFF;
		data_ptr[bi++] = (codeword >> 8) & 0xFF;
		data_ptr[bi++] = codeword & 0xFF;
	}
	
	bitrate = pocsag_bitrates[options_bitrate.selected_index()];
	
	baseband::set_fsk_data(
		codewords.size() * 32,
		2280000 / bitrate,
		4500,
		64
	);
	
	return true;
}

void POCSAGTXView::paint(Painter&) {
	message = buffer;
	text_message.set(message);
}

void POCSAGTXView::on_set_text(NavigationView& nav) {
	text_entry(nav, buffer, 16);
}

POCSAGTXView::POCSAGTXView(
	NavigationView& nav
) : nav_ (nav)
{
	uint32_t reload_address;
	uint32_t c;
	
	baseband::run_image(portapack::spi_flash::image_tag_fsktx);

	add_children({
		&labels,
		&options_bitrate,
		&field_address,
		&options_type,
		&text_message,
		&button_message,
		&progressbar,
		&tx_view
	});

	options_bitrate.set_selected_index(1);	// 1200bps
	options_type.set_selected_index(0);		// Address only
	
	// TODO: set_value for whole symfield
	reload_address = portapack::persistent_memory::pocsag_address();
	for (c = 0; c < 7; c++) {
		field_address.set_sym(6 - c, reload_address % 10);
		reload_address /= 10;
	}
	
	button_message.on_select = [this, &nav](Button&) {
		this->on_set_text(nav);
	};
	
	tx_view.on_edit_frequency = [this, &nav]() {
		auto new_view = nav.push<FrequencyKeypadView>(receiver_model.tuning_frequency());
		new_view->on_changed = [this](rf::Frequency f) {
			receiver_model.set_tuning_frequency(f);
		};
	};
	
	tx_view.on_start = [this]() {
		if (start_tx())
			tx_view.set_transmitting(true);
	};
	
	tx_view.on_stop = [this]() {
		tx_view.set_transmitting(false);
		transmitter_model.disable();
	};
	
}

} /* namespace ui */

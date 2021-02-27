/****************************************************************************
 *
 *   Copyright (c) 2021 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file Esc.hpp
 *
 * Defines basic functionality of UAVCAN v1 ESC setpoint subscription
 * (For use on a CAN-->PWM node)
 *
 * @author Jacob Crabill <jacob@flyvoly.com>
 */

#pragma once

#include <uORB/topics/output_control.h>

// DS-15 Specification Messages
#include <reg/drone/service/actuator/common/sp/Vector8_0_1.h>
#include <reg/drone/service/common/Readiness_0_1.h>

#include "Subscriber.hpp"

class UavcanEscSubscriber : public UavcanSubscriber
{
public:
	UavcanEscSubscriber(CanardInstance &ins, UavcanParamManager &pmgr, uint8_t instance = 0) :
		UavcanSubscriber(ins, pmgr, "esc", instance)
	{
		_arming_pub.get().prearmed = false;
		_arming_pub.get().armed = false;
		_arming_pub.get().timestamp = hrt_absolute_time();
		_arming_pub.update();
	};

	bool handlesID(const CanardPortID &id)
	{
		return (id == _port_id) || (id == _arming_id);
	}

	void subscribe() override
	{
		// Subscribe to messages reg.drone.service.actuator.common.sp.Vector8.0.1
		canardRxSubscribe(&_canard_instance,
				  CanardTransferKindMessage,
				  _port_id,
				  reg_drone_service_actuator_common_sp_Vector8_0_1_EXTENT_BYTES_,
				  CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
				  &_canard_sub);

		// Subscribe to messages reg.drone.service.common.Readiness.0.1
		_arming_id = static_cast<CanardPortID>(static_cast<uint32_t>(_port_id) + 1);
		canardRxSubscribe(&_canard_instance,
				  CanardTransferKindMessage,
				  _arming_id,
				  reg_drone_service_common_Readiness_0_1_EXTENT_BYTES_,
				  CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
				  &_canard_sub_readiness);
	};

	void callback(const CanardTransfer &receive) override
	{
		if (receive.port_id == _port_id) {
			// Test with Yakut:
			// export YAKUT_TRANSPORT="pyuavcan.transport.can.CANTransport(pyuavcan.transport.can.media.slcan.SLCANMedia('/dev/serial/by-id/usb-Zubax_Robotics_Zubax_Babel_23002B000E514E413431302000000000-if00', 8, 115200), 42)"
			// yakut pub 22.reg.drone.service.actuator.common.sp.Vector8.0.1 '{value: [1000, 2000, 3000, 4000, 0, 0, 0, 0]}'
			// PX4_INFO("EscCallback");

			reg_drone_service_actuator_common_sp_Vector8_0_1 esc {};
			size_t esc_size_in_bits = receive.payload_size;
			reg_drone_service_actuator_common_sp_Vector8_0_1_deserialize_(&esc, (const uint8_t *)receive.payload,
					&esc_size_in_bits);

			// double val1 = static_cast<double>(esc.value[0]);
			// double val2 = static_cast<double>(esc.value[1]);
			// double val3 = static_cast<double>(esc.value[2]);
			// double val4 = static_cast<double>(esc.value[3]);
			// PX4_INFO("values[0-3] = {%f, %f, %f, %f}", val1, val2, val3, val4);

			// Publish to the output_control_mc topic; whatever output module is configured with
			// FUNCTION_MC_MOTOR{1-8} will accept these values
			output_control_s outputs;

			outputs.timestamp = hrt_absolute_time();

			for (uint8_t i = 0; i < 8; i++) {
				outputs.value[i] = 2.f * (esc.value[i] / 8191.f) - 1.f;
			}

			_output_pub.publish(outputs);

		} else if (receive.port_id == _arming_id) {
			reg_drone_service_common_Readiness_0_1 arm {};
			size_t arm_size = receive.payload_size;
			reg_drone_service_common_Readiness_0_1_deserialize_(&arm, (const uint8_t *)receive.payload,
					&arm_size);

			actuator_armed_s armed {};

			switch (arm.value) {
			case reg_drone_service_common_Readiness_0_1_SLEEP:
				armed.armed = false;
				armed.prearmed = false;
				break;

			case reg_drone_service_common_Readiness_0_1_STANDBY:
				armed.armed = false;
				armed.prearmed = true;
				break;

			case reg_drone_service_common_Readiness_0_1_ENGAGED:
				armed.armed = true;
				armed.prearmed = true;
				break;
			}

			if (armed.prearmed != _arming_pub.get().prearmed || armed.armed != _arming_pub.get().armed) {
				_arming_pub.get().prearmed = armed.prearmed;
				_arming_pub.get().armed = armed.armed;
				_arming_pub.get().timestamp = hrt_absolute_time();
				_arming_pub.update();
			}
		}
	};

private:
	uORB::Publication<output_control_s> _output_pub{ORB_ID(output_control_mc)};

	CanardRxSubscription _canard_sub_readiness;
	CanardPortID _arming_id {CANARD_PORT_ID_UNSET};

	uORB::PublicationData<actuator_armed_s> _arming_pub{ORB_ID(actuator_armed)};
};

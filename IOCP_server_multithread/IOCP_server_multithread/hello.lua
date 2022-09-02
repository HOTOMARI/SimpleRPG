my_id = 9999
p_id = -1
direction = 0
forceMove = false
moveStack = 0

function set_object_id( id )
	my_id=id
end

function event_player_move( player_id )
	p_id = player_id
	player_x = API_get_x(p_id)
	player_y = API_get_y(p_id)
	my_x = API_get_x(my_id)
	my_y = API_get_y(my_id)
	if(forceMove==false)then
		if(player_x == my_x) then
			if(player_y == my_y) then
				API_chat(p_id, my_id, "HELLO")
				forceMove = true
				direction = math.random(3)
			end
		end
	end
end

function get_forceMove_dir()
	moveStack=moveStack+1
	if(moveStack>=3) then
		forceMove = false
		API_chat(p_id, my_id, "BYE")
		moveStack = 0
	end
	return direction
end

function get_forceMove_bool()
	return forceMove
end
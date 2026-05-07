package sdl;

private typedef JoystickPtr = hl.Abstract<"sdl_joystick">;

@:hlNative("sdl")
class Joystick {

	var ptr : JoystickPtr;

	public var id(get,never) : Int;
	public var name(get,never) : String;
	public var numAxes(get,never) : Int;
	public var numButtons(get,never) : Int;
	public var numHats(get,never) : Int;
	public var numBalls(get,never) : Int;
	public var playerIndex(get,never) : Int;
	public var product(get,never) : Int;
	public var vendor(get,never) : Int;
	public var productVersion(get,never) : Int;

	public function new( index : Int ){
		ptr = joyOpen( index );
	}

	public inline function getAxis( axisId : Int ){
		return joyGetAxis(ptr, axisId);
	}

	public inline function getHat( hatId : Int ){
		return joyGetHat(ptr, hatId);
	}

	public inline function getButton( btnId : Int ){
		return joyGetButton(ptr, btnId);
	}

	public inline function getBall( ballId : Int, dx : hl.Ref<Int>, dy : hl.Ref<Int> ) : Int {
		return joyGetBall(ptr, ballId, dx, dy);
	}

	public inline function get_id() : Int {
		return joyGetId(ptr);
	}

	public inline function get_name() : String {
		return @:privateAccess String.fromUTF8( joyGetName(ptr) );
	}

	public inline function get_numAxes() : Int {
		return joyNumAxes(ptr);
	}

	public inline function get_numButtons() : Int {
		return joyNumButtons(ptr);
	}

	public inline function get_numHats() : Int {
		return joyNumHats(ptr);
	}

	public inline function get_numBalls() : Int {
		return joyNumBalls(ptr);
	}

	public inline function get_playerIndex() : Int {
		return joyPlayerIndex(ptr);
	}

	public inline function get_product() : Int {
		return joyProduct(ptr);
	}

	public inline function get_vendor() : Int {
		return joyVendor(ptr);
	}

	public inline function get_productVersion() : Int {
		return joyProductVersion(ptr);
	}

	public function rumble( lowFreq : Int, highFreq : Int, durationMs : Int ) : Bool {
		return joyRumble(ptr, lowFreq, highFreq, durationMs);
	}

	public function close(){
		joyClose( ptr );
		ptr = null;
	}

	// Native bindings
	static function joyOpen( idx : Int ) : JoystickPtr { return null; }
	static function joyClose( joystick : JoystickPtr ) {}
	static function joyGetAxis( joystick : JoystickPtr, axisId : Int ) : Int { return 0; }
	static function joyGetHat( joystick : JoystickPtr, hatId : Int ) : Int { return 0; }
	static function joyGetButton( joystick : JoystickPtr, btnId : Int ) : Bool { return false; }
	static function joyGetBall( joystick : JoystickPtr, ballId : Int, dx : hl.Ref<Int>, dy : hl.Ref<Int> ) : Int { return 0; }
	static function joyGetId( joystick : JoystickPtr ) : Int { return -1; }
	static function joyGetName( joystick : JoystickPtr ) : hl.Bytes { return null; }
	static function joyNumAxes( joystick : JoystickPtr ) : Int { return 0; }
	static function joyNumButtons( joystick : JoystickPtr ) : Int { return 0; }
	static function joyNumHats( joystick : JoystickPtr ) : Int { return 0; }
	static function joyNumBalls( joystick : JoystickPtr ) : Int { return 0; }
	static function joyPlayerIndex( joystick : JoystickPtr ) : Int { return -1; }
	static function joyProduct( joystick : JoystickPtr ) : Int { return 0; }
	static function joyVendor( joystick : JoystickPtr ) : Int { return 0; }
	static function joyProductVersion( joystick : JoystickPtr ) : Int { return 0; }
	static function joyRumble( joystick : JoystickPtr, low : Int, high : Int, duration : Int ) : Bool { return false; }
}

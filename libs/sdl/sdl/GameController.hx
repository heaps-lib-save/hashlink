package sdl;

private typedef GamepadPtr = hl.Abstract<"sdl_gamepad">;

@:hlNative("sdl")
class GameController {

	var ptr : GamepadPtr;

	public var id(get,never) : Int;
	public var name(get,never) : String;
	public var playerIndex(get,never) : Int;
	public var product(get,never) : Int;
	public var vendor(get,never) : Int;
	public var productVersion(get,never) : Int;

	public function new( index : Int ){
		ptr = gctrlOpen( index );
	}

	public inline function getAxis( axisId : Int ){
		return gctrlGetAxis(ptr, axisId);
	}

	public inline function getButton( btnId : Int ){
		return gctrlGetButton(ptr, btnId);
	}

	public inline function get_id() : Int {
		return gctrlGetId(ptr);
	}

	public inline function get_name() : String {
		return @:privateAccess String.fromUTF8( gctrlGetName(ptr) );
	}

	public inline function get_playerIndex() : Int {
		return gctrlPlayerIndex(ptr);
	}

	public inline function get_product() : Int {
		return gctrlProduct(ptr);
	}

	public inline function get_vendor() : Int {
		return gctrlVendor(ptr);
	}

	public inline function get_productVersion() : Int {
		return gctrlProductVersion(ptr);
	}

	public function rumble( lowFreq : Int, highFreq : Int, durationMs : Int ) : Bool {
		return gctrlRumble(ptr, lowFreq, highFreq, durationMs);
	}

	public function close(){
		gctrlClose( ptr );
		ptr = null;
	}

	// Native bindings
	static function gctrlCount() : Int { return 0; }
	static function gctrlOpen( idx : Int ) : GamepadPtr { return null; }
	static function gctrlClose( controller : GamepadPtr ) {}
	static function gctrlGetAxis( controller : GamepadPtr, axisId : Int ) : Int { return 0; }
	static function gctrlGetButton( controller : GamepadPtr, btnId : Int ) : Bool { return false; }
	static function gctrlGetId( controller : GamepadPtr ) : Int { return -1; }
	static function gctrlGetName( controller : GamepadPtr ) : hl.Bytes { return null; }
	static function gctrlPlayerIndex( controller : GamepadPtr ) : Int { return -1; }
	static function gctrlProduct( controller : GamepadPtr ) : Int { return 0; }
	static function gctrlVendor( controller : GamepadPtr ) : Int { return 0; }
	static function gctrlProductVersion( controller : GamepadPtr ) : Int { return 0; }
	static function gctrlRumble( controller : GamepadPtr, low : Int, high : Int, duration : Int ) : Bool { return false; }
}

# Testing the Callback Architecture

This guide shows you how to test the new callback-based audio completion system.

## Quick Test Commands

```bash
# 1. Build the project
build.bat

# 2. Run callback unit tests
build\tests\nigamp_tests.exe --gtest_filter="CallbackArchitecture.*"

# 3. Test with a real audio file (preview mode)
build\nigamp.exe --file "tests\data\test1.mp3" --preview
```

## What to Look For

### ✅ **Success Indicators**

**Console Output You Should See:**
```
Decoder reached EOF, signaling audio engine
Audio playback completed successfully after 1234ms
Auto-advancing to next track: [next song]
```

**What This Means:**
- ✅ Decoder finished reading the file
- ✅ Audio engine detected completion when buffers emptied  
- ✅ Callback fired successfully
- ✅ Track advanced at the right time

### ❌ **Failure Indicators**

**Bad Console Output:**
```
Warning: Audio completion callback timeout after 3 seconds. Forcing track advance.
```
**What This Means:**
- ⚠️ Callback never fired
- ⚠️ Timeout fallback kicked in
- ❓ Possible issue with callback mechanism

## Testing Scenarios

### 1. **Unit Tests** (No Audio Files Needed)

```bash
# Run all callback tests
build\tests\nigamp_tests.exe --gtest_filter="CallbackArchitecture.*"

# Run specific test
build\tests\nigamp_tests.exe --gtest_filter="CallbackArchitecture.BasicCompletionFlow"
```

**What These Test:**
- ✅ Callback fires when EOF + buffers empty
- ✅ Callback doesn't fire prematurely  
- ✅ Multiple EOF signals only fire callback once
- ✅ Exception safety in callbacks
- ✅ Basic threading safety

### 2. **Integration Tests** (Real Audio Files)

```bash
# Test with preview mode (10 seconds)
build\nigamp.exe --file "tests\data\test1.mp3" --preview

# Test with full playback
build\nigamp.exe --file "tests\data\test1.mp3"

# Test with directory
build\nigamp.exe --folder "tests\data"
```

**What to Watch:**
- ⏱️ Does the track advance exactly when audio stops?
- 🔄 Do tracks advance smoothly without gaps?
- 📊 Are completion times reasonable (should be < 100ms after audio ends)?

### 3. **Edge Case Testing**

**Test Timeout Fallback:**
```bash
# This should complete normally (no timeout)
build\nigamp.exe --file "tests\data\test1.mp3" --preview
```

**Test Rapid Track Changes:**
```bash
# Rapidly press Ctrl+Alt+N to advance tracks
# Should advance immediately without waiting for callbacks
build\nigamp.exe --folder "tests\data"
```

**Test Error Handling:**
```bash
# Test with invalid file
build\nigamp.exe --file "nonexistent.mp3"
```

## Debugging Callback Issues

### **Enable Detailed Logging**

If you suspect callback issues, you can modify the code temporarily:

```cpp
// In audio_engine.cpp, add to fire_completion_callback():
std::cout << "[CALLBACK DEBUG] Firing completion callback, samples processed: " 
          << m_total_samples_processed << std::endl;

// In main.cpp, add to handle_playback_completion():
std::cout << "[CALLBACK DEBUG] Received callback: " << result.error_message 
          << ", time: " << result.completion_time.count() << "ms" << std::endl;
```

### **Check Timing**

Good timing indicators:
- **Completion time**: 10-100ms after EOF signaled
- **Track advance**: Immediate after callback
- **No timeouts**: Should never see timeout warnings

Bad timing indicators:
- **Completion time**: > 1000ms suggests buffer drain issues
- **Frequent timeouts**: Suggests callback mechanism broken
- **Premature advance**: Tracks change before audio ends

## Manual Verification Steps

### **Step 1: Basic Functionality**
1. Play a short file with `--preview`
2. Count 10 seconds
3. Verify track advances exactly when audio stops (not before/after)

### **Step 2: Buffer Drain Verification**  
1. Play a file normally
2. Watch console for "Decoder reached EOF" message
3. Listen for when audio actually stops
4. Verify "Audio playback completed successfully" appears when audio stops

### **Step 3: Safety Mechanisms**
1. If you see timeout warnings, there's a callback issue
2. The app should never get "stuck" on a track
3. Manual track changes (hotkeys) should always work immediately

## Expected Performance

| Scenario | Expected Behavior |
|----------|------------------|
| **10s preview** | Advances exactly at 10s |
| **Normal playback** | Advances when audio ends (not when file finishes decoding) |
| **Manual skip** | Immediate advance |
| **Callback timeout** | Auto-advance after 3s with warning |
| **Error cases** | Graceful handling, no crashes |

## Troubleshooting

**Problem**: Tracks advance too early
**Solution**: Check that EOF signal and buffer drain both happen

**Problem**: Tracks never advance  
**Solution**: Check timeout fallback is working

**Problem**: Crashes or exceptions
**Solution**: Check callback exception handling

**Problem**: Inconsistent timing
**Solution**: Check threading and race conditions

---

## Quick Health Check

Run this sequence to verify everything works:

```bash
# 1. Unit tests should all pass
build\tests\nigamp_tests.exe --gtest_filter="CallbackArchitecture.*"

# 2. This should play for exactly 10 seconds then advance
build\nigamp.exe --file "tests\data\test1.mp3" --preview

# 3. No timeout warnings should appear in console
```

If all three work correctly, the callback architecture is functioning properly!
package com.musicgame.player;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.os.Bundle;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;

/**
 * Launcher Activity that checks for native crash logs before starting the game.
 * If a crash log exists from a previous run, it shows the error in a dialog.
 * Otherwise, it immediately launches the NativeActivity.
 */
public class LauncherActivity extends Activity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        File crashFile = new File(getFilesDir(), "crash.txt");
        if (crashFile.exists()) {
            String message = readFile(crashFile);
            crashFile.delete();
            showCrashDialog(message);
        } else {
            launchGame();
        }
    }

    private void launchGame() {
        // Launch our NativeActivity subclass (MainActivity) instead of stock
        // NativeActivity, so onCreate can force landscape orientation before
        // the native window is sized.
        Intent intent = new Intent(this, MainActivity.class);
        intent.putExtra("android.app.lib_name", "musicgame");
        startActivity(intent);
        finish();
    }

    private void showCrashDialog(String message) {
        new AlertDialog.Builder(this)
            .setTitle("Game Crashed")
            .setMessage(message)
            .setCancelable(false)
            .setPositiveButton("Retry", (dialog, which) -> {
                launchGame();
            })
            .setNegativeButton("Exit", (dialog, which) -> {
                finish();
            })
            .show();
    }

    private String readFile(File file) {
        StringBuilder sb = new StringBuilder();
        try (BufferedReader br = new BufferedReader(new FileReader(file))) {
            String line;
            while ((line = br.readLine()) != null) {
                sb.append(line).append("\n");
            }
        } catch (Exception e) {
            sb.append("(Could not read crash log: ").append(e.getMessage()).append(")");
        }
        return sb.toString();
    }
}

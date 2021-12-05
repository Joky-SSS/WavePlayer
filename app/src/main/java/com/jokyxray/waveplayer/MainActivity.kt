package com.jokyxray.waveplayer

import android.Manifest
import android.os.AsyncTask
import android.os.Build
import android.os.Bundle
import android.os.Environment
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import com.jokyxray.waveplayer.databinding.ActivityMainBinding
import java.io.File

class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
        binding.playButton.setOnClickListener {
            onPlayButtunClick()
        }
        if (Build.VERSION.SDK_INT > Build.VERSION_CODES.M){
            requestPermissions(arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE),1)
        }
    }

    private fun onPlayButtunClick() {
        val file =
            File(Environment.getExternalStorageDirectory(), binding.fileNameEdit.text.toString())
        val playerTask = PlayerTask()
        playerTask.execute(file.absolutePath)
    }

    private inner class PlayerTask : AsyncTask<String, Void, Exception>() {
        override fun doInBackground(vararg params: String?): Exception? {
            var result: Exception? = null
            try {
                play(params[0]!!)
            } catch (e: Exception) {
                result = e
            }
            return result
        }

        override fun onPostExecute(result: Exception?) {
            result?.let {
                AlertDialog.Builder(this@MainActivity).setTitle(R.string.error_alert_title)
                    .setMessage(it.message).show()
            }
        }
    }
    private companion object{
        @JvmStatic
        private external fun play(fileName:String)
        init {
            System.loadLibrary("waveplayer")
        }
    }
}
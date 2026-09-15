package com.oleglati.slade_3_mobile

import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.recyclerview.widget.RecyclerView
import com.oleglati.slade_3_mobile.databinding.ItemWadEntryBinding

// Place this file at:
//   app/src/main/java/com/oleglati/slade_3_mobile/WadEntryAdapter.kt

data class WadEntry(val index: Int, val name: String, val size: Long, val type: String)

class WadEntryAdapter(
    private val onEntryClick: (WadEntry) -> Unit,
    private val onEntryLongClick: (WadEntry) -> Unit
) : RecyclerView.Adapter<WadEntryAdapter.ViewHolder>() {

    private var entries: List<WadEntry> = emptyList()

    fun submitList(newEntries: List<WadEntry>) {
        entries = newEntries
        notifyDataSetChanged()
    }

    inner class ViewHolder(val binding: ItemWadEntryBinding) :
        RecyclerView.ViewHolder(binding.root)

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
        val binding = ItemWadEntryBinding.inflate(
            LayoutInflater.from(parent.context), parent, false
        )
        return ViewHolder(binding)
    }

    override fun onBindViewHolder(holder: ViewHolder, position: Int) {
        val entry = entries[position]
        holder.binding.entryName.text = entry.name
        holder.binding.entrySize.text = "${entry.type} · ${entry.size} B"
        holder.itemView.setOnClickListener { onEntryClick(entry) }
        // Phase 7: long-press brings up Rename/Delete -- a tap is still
        // "preview this", so edit actions get a separate gesture rather
        // than overloading the existing click.
        holder.itemView.setOnLongClickListener { onEntryLongClick(entry); true }
    }

    override fun getItemCount(): Int = entries.size
}

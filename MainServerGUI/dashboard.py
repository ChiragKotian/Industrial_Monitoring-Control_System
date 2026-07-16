import streamlit as st
import pandas as pd
import plotly.express as px
import os
from datetime import datetime
import time  # <-- ADD THIS

# --- 1. PAGE CONFIGURATION ---
st.set_page_config(page_title="HPCL Substation Dashboard", page_icon="🏭", layout="wide")
st.title("🏭 HPCL MR Substation Edge-Monitoring")
st.markdown("Secure, Air-Gapped Local Telemetry Network")

DB_FILE = "live_database.csv"

# --- 2. THE AL-CAN PAYLOAD PARSER ---
# This safely reads the text file without locking it, so the listener can keep writing to it
@st.cache_data(ttl=2) # Cache expires every 2 seconds to simulate a live feed!
def load_and_parse_data(filepath):
    if not os.path.exists(filepath):
        return pd.DataFrame()

    parsed_data = []
    with open(filepath, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line: continue
            
            # Split your 4 main CSV columns: Time, Node, Group, Payload
            parts = line.split(',')
            if len(parts) >= 4:
                timestamp_str = parts[0]
                node_id = parts[1]
                group_id = parts[2]
                payload = ",".join(parts[3:]) # Re-join in case payload has commas
                
                # Convert string to actual Python datetime
                dt = pd.to_datetime(timestamp_str, errors='coerce', dayfirst=True)
                
                row = {
                    "Timestamp": dt,
                    "Node_ID": int(node_id),
                    "Group_ID": int(group_id),
                    "Raw_Payload": payload
                }
                
                # Dynamically extract sensors (e.g., "OBJ1:47.8; AMB:33.4")
                sensors = payload.split(';')
                for s in sensors:
                    if ':' in s:
                        key, val = s.split(':', 1)
                        try:
                            row[key.strip()] = float(val.strip())
                        except ValueError:
                            row[key.strip()] = val.strip() # Catch text statuses like "SYS:OK"
                            
                parsed_data.append(row)
                
    df = pd.DataFrame(parsed_data)
    if not df.empty:
        # Sort chronologically and drop any accidental duplicate packets
        df = df.sort_values(by="Timestamp").drop_duplicates()
    return df

df = load_and_parse_data(DB_FILE)

# --- 3. SIDEBAR CONTROLS & SD CARD IMPORTER ---
with st.sidebar:
    st.header("🎛️ Network Controls")
    
    # --- NEW: Live Auto-Refresh Toggle ---
    auto_refresh = st.checkbox("🟢 Auto-Refresh (Live Mode)", value=True)
    
    if st.button("🔄 Force Refresh", use_container_width=True):
        st.cache_data.clear()
        
    st.divider()
    
    # SD Card "Gap-Fill" Feature
    st.header("💾 SD Offline Sync")
    st.markdown("Import `telemetry.csv` directly from an LMP MicroSD to patch network outages.")
    uploaded_file = st.file_uploader("Upload SD Card Data", type=['csv'])
    if uploaded_file is not None:
        new_data = uploaded_file.getvalue().decode("utf-8")
        with open(DB_FILE, "a", encoding="utf-8") as f:
            f.write("\n" + new_data.strip())
        st.success("SD Data Merged Successfully!")
        st.cache_data.clear()

# --- 4. MAIN DASHBOARD ---
if df.empty:
    st.warning("Listening for LoRa Packets... No data in database yet.")
else:
    # Top KPI Metrics
    total_nodes = df['Node_ID'].nunique()
    latest_time = df['Timestamp'].max().strftime("%Y-%m-%d %H:%M:%S")
    
    col1, col2, col3 = st.columns(3)
    col1.metric("Active LMPs", total_nodes)
    col2.metric("Network Status", "🟢 ONLINE")
    col3.metric("Last Packet Received", latest_time)
    
    # Dashboard Tabs
    tab1, tab2, tab3 = st.tabs(["📈 Live Telemetry", "🏥 Substation Health Matrix", "🚨 Audit Log"])
    
    with tab1:
        st.subheader("Thermal & Environmental Trends")
        selected_node = st.selectbox("Select Substation Node to Monitor:", sorted(df['Node_ID'].unique()))
        
        node_df = df[df['Node_ID'] == selected_node].copy()
        
        if 'OBJ1' in node_df.columns and 'AMB' in node_df.columns:
            # Melt the dataframe so Plotly can graph multiple lines easily
            chart_df = node_df[['Timestamp', 'OBJ1', 'AMB']].dropna().melt(id_vars='Timestamp', var_name='Sensor', value_name='Temperature (°C)')
            fig = px.line(chart_df, x='Timestamp', y='Temperature (°C)', color='Sensor', title=f"Thermal Graph: Node {selected_node}")
            st.plotly_chart(fig, use_container_width=True)
            
        if 'RH' in node_df.columns:
            hum_df = node_df[['Timestamp', 'RH']].dropna()
            if not hum_df.empty:
                fig2 = px.line(hum_df, x='Timestamp', y='RH', title=f"Humidity Profile: Node {selected_node}", color_discrete_sequence=['#00d2ff'])
                st.plotly_chart(fig2, use_container_width=True)

    with tab2:
        st.subheader("Substation Node Health Matrix")
        # Find the last time each node reported in
        health_df = df.groupby('Node_ID')['Timestamp'].max().reset_index()
        health_df.columns = ['LMP ID', 'Last Seen']
        
        # Calculate time since last packet
        now = datetime.now()
        health_df['Status'] = health_df['Last Seen'].apply(
            lambda x: "🟢 HEALTHY" if (now - x).total_seconds() < 120 else "🔴 OFFLINE"
        )
        st.dataframe(health_df, use_container_width=True, hide_index=True)

    with tab3:
        st.subheader("System Audit & Alarm Log")
        # Simple threshold rule: If OBJ1 goes over 60°C, it's an alarm!
        if 'OBJ1' in df.columns:
            alarms = df[df['OBJ1'] > 60.0].copy()
            alarms['Alert Type'] = "High Temperature Threshold Crossed"
            st.dataframe(alarms[['Timestamp', 'Node_ID', 'Alert Type', 'Raw_Payload']].sort_values('Timestamp', ascending=False), use_container_width=True, hide_index=True)
            
            # Management Export Feature
            csv_export = df.to_csv(index=False).encode('utf-8')
            st.download_button(label="📥 Export Full Database to CSV (For SAP / Excel)", data=csv_export, file_name='HPCL_Substation_Log.csv', mime='text/csv')


if auto_refresh:
    time.sleep(2) # Wait 2 seconds
    st.cache_data.clear() # Dump the old cache
    st.rerun() # Command the browser to instantly reload the page!
import streamlit as st
import pandas as pd
import plotly.express as px
import os
import time
from datetime import datetime

# --- 1. PAGE CONFIGURATION ---
st.set_page_config(page_title="HPCL Substation Dashboard", page_icon="🏭", layout="wide")
st.title("🏭 HPCL MR Substation Edge-Monitoring")
st.markdown("Secure, Air-Gapped Local Telemetry Network")

LIVE_FILE = "live_database.csv"
SD_FILE = "sd_database.csv"

# --- 2. SIDEBAR CONTROLS ---
with st.sidebar:
    st.header("🎛️ Data Source")
    data_mode = st.radio(
        "Select Active View:",
        ["Live Telemetry Only", "SD Card Data Only", "Merged (Live + SD)"],
        index=0
    )
    
    st.divider()
    st.header("🚨 Alarm Settings")
    alarm_threshold = st.number_input(
        "Temp Threshold (°C)", 
        min_value=20.0, max_value=150.0, value=60.0, step=1.0,
        help="Any temperature exceeding this will trigger the Audit Log."
    )
    
    st.divider()
    st.header("💾 SD Offline Sync")
    uploaded_file = st.file_uploader("Upload SD Card Data (telemetry.csv)", type=['csv'])
    if uploaded_file is not None:
        new_data = uploaded_file.getvalue().decode("utf-8")
        # Overwrite the temporary SD database with the new upload
        with open(SD_FILE, "w", encoding="utf-8") as f:
            f.write(new_data.strip() + "\n")
        st.success("SD Data Loaded Successfully!")
        st.cache_data.clear()

    st.divider()
    auto_refresh = st.checkbox("🟢 Auto-Refresh (Live Mode)", value=True)
    if st.button("🔄 Force Refresh", use_container_width=True):
        st.cache_data.clear()


# --- 3. DYNAMIC PARSER & DELTA LOADING ---
# Setup persistent memory for the live feed so we don't re-read old data
if "live_df" not in st.session_state:
    st.session_state.live_df = pd.DataFrame()
    st.session_state.live_file_pos = 0

def extract_payload(lines_iterable):
    """Helper function to parse our AL-CAN text strings into structured rows"""
    parsed_data = []
    for line in lines_iterable:
        line = line.strip()
        if not line: continue
        parts = line.split(',')
        if len(parts) >= 4:
            timestamp_str, node_id, group_id = parts[0], parts[1], parts[2]
            payload = ",".join(parts[3:])
            
            dt = pd.to_datetime(timestamp_str, errors='coerce', dayfirst=True)
            row = {"Timestamp": dt, "Node_ID": int(node_id), "Group_ID": int(group_id), "Raw_Payload": payload}
            
            for s in payload.split(';'):
                if ':' in s:
                    key, val = s.split(':', 1)
                    try: row[key.strip()] = float(val.strip())
                    except ValueError: row[key.strip()] = val.strip()
            parsed_data.append(row)
    return pd.DataFrame(parsed_data)

@st.cache_data
def load_static_sd_data():
    """Reads the SD Card data. Cached so it only runs once per upload!"""
    if os.path.exists(SD_FILE):
        with open(SD_FILE, 'r', encoding='utf-8') as f:
            return extract_payload(f)
    return pd.DataFrame()

def load_and_parse_data(mode):
    df_list = []
    
    # 1. LIVE DATA (DELTA APPENDING)
    if mode in ["Live Telemetry Only", "Merged (Live + SD)"]:
        if os.path.exists(LIVE_FILE):
            current_size = os.path.getsize(LIVE_FILE)
            
            # If file was deleted or cleared manually, reset our memory
            if current_size < st.session_state.live_file_pos:
                st.session_state.live_file_pos = 0
                st.session_state.live_df = pd.DataFrame()
            
            # If the file grew, read ONLY the new bytes!
            if current_size > st.session_state.live_file_pos:
                with open(LIVE_FILE, "r", encoding="utf-8") as f:
                    f.seek(st.session_state.live_file_pos) # Skip straight to the new data
                    new_df = extract_payload(f)
                    st.session_state.live_file_pos = f.tell() # Save our new position
                
                # Append the newly parsed micro-batch to the master live DataFrame
                if not new_df.empty:
                    if st.session_state.live_df.empty:
                        st.session_state.live_df = new_df
                    else:
                        st.session_state.live_df = pd.concat([st.session_state.live_df, new_df], ignore_index=True)
        
        df_list.append(st.session_state.live_df)

    # 2. SD CARD DATA
    if mode in ["SD Card Data Only", "Merged (Live + SD)"]:
        sd_df = load_static_sd_data()
        df_list.append(sd_df)
        
    # 3. MERGE & CLEANUP
    if df_list:
        combined = pd.concat(df_list, ignore_index=True)
        if not combined.empty:
            combined = combined.sort_values(by="Timestamp").drop_duplicates(subset=["Timestamp", "Node_ID"])
        return combined
    
    return pd.DataFrame()

df = load_and_parse_data(data_mode)

# --- 4. MAIN DASHBOARD ---
if df.empty:
    st.info(f"No data available for '{data_mode}'. Please check gateway connection or upload an SD file.")
else:
    # Top KPI Metrics
    total_nodes = df['Node_ID'].nunique()
    latest_time = df['Timestamp'].max().strftime("%Y-%m-%d %H:%M:%S")
    
    col1, col2, col3 = st.columns(3)
    col1.metric("Active LMPs in View", total_nodes)
    col2.metric("Data Source", "Merged" if "Merged" in data_mode else "Isolated")
    col3.metric("Last Packet Timestamp", latest_time)
    
    tab1, tab2, tab3 = st.tabs(["📈 Telemetry Graphs", "🏥 Substation Health Matrix", "🚨 Audit Log"])
    
    with tab1:
        st.subheader("Thermal & Environmental Trends")
        
        # --- UI ROW FOR CONTROLS ---
        colA, colB = st.columns([2, 1])
        with colA:
            selected_node = st.selectbox("Select Substation Node to Monitor:", sorted(df['Node_ID'].unique()))
        with colB:
            # FIX: The "Squished Graph" 1970/2024 Glitch Toggle
            graph_view = st.radio("Timeline View:", ["Live Window (Last 30)", "Full History"], horizontal=True)
        
        node_df = df[df['Node_ID'] == selected_node].copy()
        
        # Apply the sliding window filter
        if graph_view == "Live Window (Last 30)":
            node_df = node_df.tail(30)
        
        if 'OBJ1' in node_df.columns and 'AMB' in node_df.columns:
            chart_df = node_df[['Timestamp', 'OBJ1', 'AMB']].dropna().melt(id_vars='Timestamp', var_name='Sensor', value_name='Temperature (°C)')
            
            fig = px.line(chart_df, x='Timestamp', y='Temperature (°C)', color='Sensor', title=f"Thermal Graph: Node {selected_node}", markers=True)
            # Add a visual horizontal red line for our dynamic threshold!
            fig.add_hline(y=alarm_threshold, line_dash="dash", line_color="red", annotation_text="ALARM THRESHOLD")
            
            st.plotly_chart(fig, use_container_width=True)
            
        if 'RH' in node_df.columns:
            hum_df = node_df[['Timestamp', 'RH']].dropna()
            if not hum_df.empty:
                fig2 = px.line(hum_df, x='Timestamp', y='RH', title=f"Humidity Profile: Node {selected_node}", color_discrete_sequence=['#00d2ff'], markers=True)
                st.plotly_chart(fig2, use_container_width=True)

    with tab2:
        st.subheader("Substation Node Health Matrix")
        health_df = df.groupby('Node_ID')['Timestamp'].max().reset_index()
        health_df.columns = ['LMP ID', 'Last Seen']
        
        now = datetime.now()
        health_df['Status'] = health_df['Last Seen'].apply(
            lambda x: "🟢 HEALTHY" if (now - x).total_seconds() < 120 else "🔴 OFFLINE / STALE"
        )
        st.dataframe(health_df, use_container_width=True, hide_index=True)

    with tab3:
        st.subheader(f"System Audit & Alarm Log (Threshold: > {alarm_threshold}°C)")
        
        if 'OBJ1' in df.columns:
            # Dynamically filter based on the user's sidebar input!
            alarms = df[df['OBJ1'] > alarm_threshold].copy()
            alarms['Alert Type'] = f"High Temp (> {alarm_threshold}°C)"
            
            if alarms.empty:
                st.success("No alarms triggered in the current dataset. All systems nominal.")
            else:
                st.error(f"{len(alarms)} High Temperature Events Detected!")
                st.dataframe(alarms[['Timestamp', 'Node_ID', 'Alert Type', 'OBJ1', 'Raw_Payload']].sort_values('Timestamp', ascending=False), use_container_width=True, hide_index=True)
            
            csv_export = df.to_csv(index=False).encode('utf-8')
            st.download_button(label="📥 Export Current View to CSV (For SAP / Excel)", data=csv_export, file_name='HPCL_Substation_Data.csv', mime='text/csv')

# --- 5. THE LIVE AUTO-REFRESH LOOP ---
if auto_refresh:
    time.sleep(2)
    st.cache_data.clear()
    st.rerun()